/*
NOTE: This ingenia example works with just one slave in the bus,
feel free to adapt it if you need to control more than one slave.
*/

#include <iostream>
#include <cstring>
#include <argparse/argparse.hpp>

#include "kickcat/Bus.h"
#include "kickcat/Link.h"
#include "kickcat/Prints.h"
#include "kickcat/helpers.h"
#include "kickcat/CoE/CiA/DS402/StateMachine.h"

#include "CanOpenErrors.h"
#include "IngeniaProtocol.h"


using namespace kickcat;

int main(int argc, char *argv[])
{
    argparse::ArgumentParser program("ingenia_control");

    std::string nom_interface_name;
    program.add_argument("-i", "--interface")
        .help("network interface name")
        .required()
        .store_into(nom_interface_name);

    std::string red_interface_name;
    program.add_argument("-r", "--redundancy")
        .help("redundancy network interface name")
        .default_value(std::string{""})
        .store_into(red_interface_name);

    try
    {
        program.parse_args(argc, argv);
    }
    catch (const std::runtime_error& err)
    {
        std::cerr << err.what() << std::endl;
        std::cerr << program;
        return 1;
    }

    std::shared_ptr<AbstractSocket> socket_nominal;
    std::shared_ptr<AbstractSocket> socket_redundancy;
    try
    {
        auto [nominal, redundancy] = createSockets(nom_interface_name, red_interface_name);
        socket_nominal = nominal;
        socket_redundancy = redundancy;
    }
    catch (std::exception const& e)
    {
        std::cerr << e.what() << std::endl;
        return 1;
    }

    auto report_redundancy = []()
    {
        printf("Redundancy has been activated due to loss of a cable \n");
    };

    std::shared_ptr<Link> link = std::make_shared<Link>(socket_nominal, socket_redundancy, report_redundancy);
    link->setTimeout(2ms);
    link->checkRedundancyNeeded();

    Bus bus(link);

    uint8_t io_buffer[2048];
    try
    {
        bus.init();

        for (auto& slave: bus.slaves())
        {
            printInfo(slave);
            printESC(slave);
        }

        // Map TXPDO
        mapPDO(bus, bus.slaves().at(0), 0x1A00, pdo::tx_mapping, pdo::tx_mapping_count, 0x1C13);
        // Map RXPDO
        mapPDO(bus, bus.slaves().at(0), 0x1600, pdo::rx_mapping, pdo::rx_mapping_count, 0x1C12);

        bus.createMapping(io_buffer);

        printf("Request SAFE OP\n");
        bus.requestState(State::SAFE_OP);
        bus.waitForState(State::SAFE_OP, 1s);
    }
    catch (ErrorAL const &e)
    {
        std::cerr << e.what() << ": " << ALStatus_to_string(e.code()) << std::endl;
        return 1;
    }
    catch (std::exception const &e)
    {
        std::cerr << e.what() << std::endl;
        return 1;
    }

    auto callback_error = [](DatagramState const & ds)
    {
        THROW_ERROR_DATAGRAM("something bad happened", ds);
    };
    auto false_alarm = [](DatagramState const &)
    { printf("previous error was a false alarm"); };

    try
    {
        bus.processDataRead(callback_error);
        bus.processDataWrite(false_alarm);
    }
    catch (...)
    {
    }

    try
    {
        auto cyclic_process_data = [&]()
        {
            auto noop =[](DatagramState const&){};
            bus.processDataRead (noop);
            bus.processDataWrite(noop);
        };

        bus.requestState(State::OPERATIONAL);
        bus.waitForState(State::OPERATIONAL, 100ms, cyclic_process_data);
    }
    catch (ErrorAL const &e)
    {
        std::cerr << e.what() << ": " << ALStatus_to_string(e.code()) << std::endl;
        return 1;
    }
    catch (std::exception const &e)
    {
        std::cerr << e.what() << std::endl;
        return 1;
    }

    link->setTimeout(1500us);

    Slave &ingenia = bus.slaves().at(0);
    pdo::Output *output_pdo = reinterpret_cast<pdo::Output *>(ingenia.output.data);
    pdo::Input *input_pdo = reinterpret_cast<pdo::Input *>(ingenia.input.data);
    CoE::CiA::DS402::StateMachine ingenia_state_machine;

    printf("mapping: input(%d) output(%d)\n", ingenia.input.bsize, ingenia.output.bsize);

    ingenia_state_machine.enable();

    // Setting a small torque
    output_pdo->mode_of_operation = 5;
    output_pdo->target_torque = 0.03;
    output_pdo->max_current = 1.7;
    output_pdo->target_position = 0;

    constexpr int64_t LOOP_NUMBER = 12 * 3600 * 1000; // 12h
    int64_t last_error = 0;
    for (int64_t i = 0; i < LOOP_NUMBER; ++i)
    {
        sleep(2ms);

        try
        {
            bus.sendLogicalRead(callback_error);  // Update inputPDO
            bus.sendLogicalWrite(callback_error); // Update outputPDO
            bus.sendMailboxesReadChecks(callback_error);
            bus.sendReadMessages(callback_error); // Get emergencies

            bus.finalizeDatagrams();
            bus.processAwaitingFrames();
        }
        catch (kickcat::ErrorDatagram const& e)
        {
            int64_t delta = i - last_error;
            last_error = i;
            std::cerr << e.what() << ": " << toString(e.state()) << " at " << i << " delta: " << delta << std::endl;
        }
        catch (std::exception const& e)
        {
            int64_t delta = i - last_error;
            last_error = i;
            std::cerr << e.what() << " at " << i << " delta: " << delta << std::endl;
        }

        // Update Ingenia at each step to receive emergencies in case of failure
        ingenia_state_machine.update(input_pdo->status_word);
        output_pdo->control_word = ingenia_state_machine.controlWord();
        if (ingenia.mailbox.emergencies.size() > 0)
        {
            for (auto &em : ingenia.mailbox.emergencies)
            {
                std::cerr << "*~~~ Emergency received @ " << i << " ~~~*" << std::endl;
                std::cerr << registerToError(em.error_register);
                std::cerr << codeToError(em.error_code);
            }
            ingenia.mailbox.emergencies.resize(0);
        }
    }

    return 0;
}
