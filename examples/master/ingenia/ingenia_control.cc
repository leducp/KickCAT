/*
NOTE: This ingenia example works with just one slave in the bus, 
feel free to adapt it if you need to control more than one slave.
*/

#include <iostream>
#include <cstring>

#include "kickcat/Bus.h"
#include "kickcat/Link.h"
#include "kickcat/Prints.h"
#include "kickcat/SocketNull.h"
#include "kickcat/helpers.h"

#include "CanOpenErrors.h"
#include "CanOpenStateMachine.h"
#include "IngeniaProtocol.h"

#ifdef __linux__
    #include "kickcat/OS/Linux/Socket.h"
#elif __PikeOS__
    #include "kickcat/OS/PikeOS/Socket.h"
#elif __MINGW64__
    #include "kickcat/OS/Windows/Socket.h"
#else
#error "Unknown platform"
#endif


using namespace kickcat;

int main(int argc, char *argv[])
{
    if (argc != 3 and argc != 2)
    {
        printf("usage redundancy mode : ./test NIC_nominal NIC_redundancy\n");
        printf("usage no redundancy mode : ./test NIC_nominal\n");
        return 1;
    }

    std::shared_ptr<AbstractSocket> socket_redundancy;
    std::string red_interface_name = "null";
    std::string nom_interface_name = argv[1];

    if (argc == 2)
    {
        printf("No redundancy mode selected \n");
        socket_redundancy = std::make_shared<SocketNull>();
    }
    else
    {
        socket_redundancy = std::make_shared<Socket>();
        red_interface_name = argv[2];
    }

    selectInterface(nom_interface_name, red_interface_name);

    auto socket_nominal = std::make_shared<Socket>();
    try
    {
        socket_nominal->open(nom_interface_name);
        socket_redundancy->open(red_interface_name);
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
        bus.init();

        for (auto& slave: bus.slaves())
        {
            printInfo(slave);
            printESC(slave);
        }

        // Prepare mapping for ingenia
        const auto mapPDO = [&](const uint8_t slaveId, const uint16_t PDO_map, const uint32_t *data, const uint32_t dataSize, const uint32_t SM_map) -> void
        {
            uint8_t zeroU8 = 0;

/*
            // Unmap previous registers, setting 0 in PDO_MAP subindex 0
            bus.writeSDO(bus.slaves().at(slaveId), PDO_map, 0, false, const_cast<uint8_t *>(&zeroU8), sizeof(zeroU8));
            // Modify mapping, setting register address in PDO's subindexes from 0x1A00:01
            for (uint32_t i = 0; i < dataSize; i++)
            {
                uint8_t subIndex = static_cast<uint8_t>(i + 1);
                bus.writeSDO(bus.slaves().at(slaveId), PDO_map, subIndex, false, const_cast<uint32_t *>(&data[i]), sizeof(data[i]));
            }
            // Enable mapping by setting number of registers in PDO_MAP subindex 0
            uint8_t pdoMapSize = static_cast<uint8_t>(dataSize);
            bus.writeSDO(bus.slaves().at(slaveId), PDO_map, 0, false, const_cast<uint8_t *>(&pdoMapSize), sizeof(pdoMapSize));
*/

            uint8_t buffer[1024];
            std::memcpy(buffer + 2, data, dataSize * 4);
            buffer[0] = dataSize;
            bus.writeSDO(bus.slaves().at(slaveId), PDO_map, 0, true, buffer, dataSize * 4 + 2);

            // Set PDO mapping to SM
            // Unmap previous mappings, setting 0 in SM_MAP subindex 0
            bus.writeSDO(bus.slaves().at(slaveId), SM_map, 0, false, const_cast<uint8_t *>(&zeroU8), sizeof(zeroU8));
            // Write first mapping (PDO_map) address in SM_MAP subindex 1
            bus.writeSDO(bus.slaves().at(slaveId), SM_map, 1, false, const_cast<uint16_t *>(&PDO_map), sizeof(PDO_map));
            // Save mapping count in SM (here only one PDO_MAP)
            uint8_t pdoMapSize = 1;
            bus.writeSDO(bus.slaves().at(slaveId), SM_map, 0, false, const_cast<uint8_t *>(&pdoMapSize), sizeof(pdoMapSize));
        };

        // Map TXPDO
        mapPDO(0, 0x1A00, pdo::tx_mapping, pdo::tx_mapping_count, 0x1C13);
        // Map RXPDO
        mapPDO(0, 0x1600, pdo::rx_mapping, pdo::rx_mapping_count, 0x1C12);

        bus.createMapping(io_buffer);

        printf("Request SAFE OP\n");
        bus.requestState(State::SAFE_OP);
        bus.waitForState(State::SAFE_OP, 1s);
    }
    catch (ErrorCode const &e)
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
        bus.requestState(State::OPERATIONAL);
        bus.waitForState(State::OPERATIONAL, 100ms);
    }
    catch (ErrorCode const &e)
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
    CANOpenStateMachine ingenia_state_machine;

    printf("mapping: input(%d) output(%d)\n", ingenia.input.bsize, ingenia.output.bsize);

    ingenia_state_machine.setCommand(CANOpenCommand::ENABLE);

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
        output_pdo->control_word = ingenia_state_machine.getControlWord();
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
