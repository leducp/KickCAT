#include <iostream>
#include <fstream>
#include <argparse/argparse.hpp>

#include "kickcat/Link.h"
#include "kickcat/Bus.h"
#include "kickcat/Prints.h"
#include "kickcat/helpers.h"
#include "kickcat/MailboxSequencer.h"

using namespace kickcat;

int main(int argc, char *argv[])
{
    argparse::ArgumentParser program("hw_test_bench");

    std::string nom_interface_name;
    program.add_argument("-i", "--interface")
        .help("network interface name")
        .required()
        .store_into(nom_interface_name);

    int expected_slaves = 2;
    program.add_argument("-s", "--slaves")
        .help("expected number of slaves")
        .default_value(2)
        .scan<'i', int>()
        .store_into(expected_slaves);

    try
    {
        program.parse_args(argc, argv);
    }
    catch (const std::runtime_error &err)
    {
        std::cerr << err.what() << std::endl;
        std::cerr << program;
        return 1;
    }

    std::shared_ptr<AbstractSocket> socket_nominal;
    std::shared_ptr<AbstractSocket> socket_redundancy;
    try
    {
        auto [nominal, redundancy] = createSockets(nom_interface_name, "");
        socket_nominal = nominal;
        socket_redundancy = redundancy;
    }
    catch (std::exception const &e)
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
        std::cout << "Initializing EtherCAT Master on " << nom_interface_name << "..." << std::endl;
        bus.init(100ms);

        if (bus.slaves().size() != static_cast<size_t>(expected_slaves))
        {
            std::cerr << "Error: Expected " << expected_slaves << " slaves, but found " << bus.slaves().size() << std::endl;
            return 1;
        }

        bus.createMapping(io_buffer);

        auto cyclic_process_data = [&]()
        {
            auto noop = [](DatagramState const &) {};
            bus.processDataRead(noop);
            bus.processDataWrite(noop);
        };

        bus.requestState(State::SAFE_OP);
        bus.waitForState(State::SAFE_OP, 1s);

        for (auto &slave : bus.slaves())
        {
            for (int32_t i = 0; i < slave.output.bsize; ++i)
            {
                slave.output.data[i] = 0xBB;
            }
        }
        cyclic_process_data();

        bus.requestState(State::OPERATIONAL);
        bus.waitForState(State::OPERATIONAL, 1s, cyclic_process_data);
    }
    catch (std::exception const &e)
    {
        std::cerr << e.what() << std::endl;
        return 1;
    }

    auto callback_error = [](DatagramState const &)
    { THROW_SYSTEM_ERROR("[ERROR] COM Failure (not sure if this is correct might be better to check datagram state ok?)"); };
    link->setTimeout(10ms);
    MailboxSequencer mailbox_sequencer(bus);

    while (true)
    {
        sleep(1ms);

        try
        {
            bus.sendLogicalRead(callback_error);
            bus.sendLogicalWrite(callback_error);
            bus.sendRefreshErrorCounters(callback_error);
            mailbox_sequencer.step(callback_error);
            bus.finalizeDatagrams();

            bus.processAwaitingFrames();
        }
        catch (std::exception const &e)
        {
            // Lost frame should we add some kinda stat metric for that ?
            std::cerr << "\n[EXCEPTION] " << e.what() << std::endl;
        }
    }

    return 0;
}
