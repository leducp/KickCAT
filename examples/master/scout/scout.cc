#include <iostream>
#include <cstring>
#include <cmath>

#include "kickcat/Bus.h"
#include "kickcat/Link.h"
#include "kickcat/Prints.h"
#include "kickcat/helpers.h"
#include "kickcat/OS/Timer.h"

#include "kickcat/Diagnostics.h"

#include "CanOpenErrors.h"
#include "CanOpenStateMachine.h"
#include "ScoutProtocol.h"

#include <rtm/probe.h>
#include <rtm/io/posix/local_socket.h>


using namespace kickcat;



int main(int argc, char *argv[])
{
    if (argc != 3 and argc != 2)
    {
        printf("usage redundancy mode : ./test NIC_nominal NIC_redundancy\n");
        printf("usage no redundancy mode : ./test NIC_nominal\n");
        return 1;
    }

    rtm::Probe probe;
    {
        auto io = std::make_unique<rtm::LocalSocket>();
        auto rc = io->open(rtm::access::Mode::READ_WRITE);
        if (rc)
        {
            printf("io open() error: %s\n", rc.message().c_str());
            return 1;
        }

        probe.init("scout", "op",
                since_epoch(), 1ms, 42,
                std::move(io));
    }

    std::string red_interface_name = "";
    std::string nom_interface_name = argv[1];
    if (argc == 3)
    {
        red_interface_name = argv[2];
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
    nanoseconds sync_point = 0ns;
    nanoseconds dc_cycle = 1ms;


    uint8_t io_buffer[2048];
    try
    {
        bus.init(100ms);

        /*
        for (auto& slave: bus.slaves())
        {
            printInfo(slave);
            printESC(slave);
        }
        */

        //sync_point = bus.enableDC(dc_cycle, dc_cycle / 2, 100ms);


        const auto mapPDO = [&](const uint8_t slaveId, const uint16_t PDO_map, uint32_t const* mapping, uint8_t mapping_count, const uint32_t SM_map) -> void
        {
            auto& slave = bus.slaves().at(slaveId);
            uint8_t zeroU8 = 0;

            // Unmap previous registers, setting 0 in PDO_MAP subindex 0
            bus.writeSDO(slave, PDO_map, 0, Bus::Access::PARTIAL, &zeroU8, sizeof(zeroU8));
            // Modify mapping, setting register address in PDO's subindexes
            for (uint8_t i = 0; i < mapping_count; ++i)
            {
                bus.writeSDO(slave, PDO_map, i + 1, Bus::Access::PARTIAL, mapping + i, sizeof(uint32_t));
            }
            // Enable mapping by setting number of registers in PDO_MAP subindex 0
            bus.writeSDO(slave, PDO_map, 0, Bus::Access::PARTIAL, &mapping_count, sizeof(mapping_count));


            // Set PDO mapping to SM
            // Unmap previous mappings, setting 0 in SM_MAP subindex 0
            bus.writeSDO(slave, SM_map, 0, Bus::Access::PARTIAL, &zeroU8, sizeof(zeroU8));
            // Write first mapping (PDO_map) address in SM_MAP subindex 1
            bus.writeSDO(slave, SM_map, 1, Bus::Access::PARTIAL, &PDO_map, sizeof(PDO_map));
            // Save mapping count in SM (here only one PDO_MAP)
            uint8_t pdoMapSize = 1;
            bus.writeSDO(slave, SM_map, 0, Bus::Access::PARTIAL, &pdoMapSize, sizeof(pdoMapSize));
        };

        // Map TXPDO
        mapPDO(0, 0x1A00, pdo::tx_mapping, pdo::tx_mapping_count, 0x1C13);
        // Map RXPDO
        mapPDO(0, 0x1600, pdo::rx_mapping, pdo::rx_mapping_count, 0x1C12);

        printf("mapping\n");
        bus.createMapping(io_buffer);

        printf("Request SAFE OP\n");
        bus.requestState(State::SAFE_OP);
        bus.waitForState(State::SAFE_OP, 100ms);

        uint16_t speed_max = 30000;
        uint32_t mode_size = 2;
        for (int j = 0; j < 1; ++j)
        {
            bus.writeSDO(bus.slaves().at(j), 0x3401, 0, Bus::Access::PARTIAL, &speed_max, mode_size);
        }
    }
    catch (ErrorAL const &e)
    {
        std::cerr << e.what() << ": " << ALStatus_to_string(e.code()) << std::endl;
        return 1;
    }
    catch (ErrorCoE const &e)
    {
        std::cerr << e.what() << ": " << CoE::SDO::abort_to_str(e.code()) << std::endl;
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
    {
        //printf("- previous error was a false alarm - ");
    };

    CANOpenStateMachine state_machine;
    Slave& motor = bus.slaves().at(0);
    pdo::Output* output_pdo = reinterpret_cast<pdo::Output *>(motor.output.data);
    pdo::Input*  input_pdo  = reinterpret_cast<pdo::Input *>(motor.input.data);
    printf("mapping: input(%d) output(%d)\n", motor.input.bsize, motor.output.bsize);

    output_pdo->mode_of_operation = 0xA;
    output_pdo->min_torque = 6000;
    output_pdo->max_torque = 6000;


    state_machine.setCommand(CANOpenCommand::DISABLE);

    Timer timer{dc_cycle};
    timer.start(sync_point);

    for (int i = 0; i < 10; ++i)
    {
        bus.processDataRead(false_alarm);
        bus.processDataWrite(false_alarm);

        timer.wait_next_tick();
    }

    while (true)
    {
        try
        {
            printf("Go to OP\n");
            bus.requestState(State::OPERATIONAL);
        }
        catch (ErrorAL const &e)
        {
            std::cerr << e.what() << ": " << ALStatus_to_string(e.code()) << std::endl;
            return 1;
        }
        catch (ErrorCoE const &e)
        {
            std::cerr << e.what() << ": " << CoE::SDO::abort_to_str(e.code()) << std::endl;
            return 1;
        }
        catch (std::exception const &e)
        {
            std::cerr << e.what() << std::endl;
            continue;
        }
        break;
    }

    link->setTimeout(1500us);

    int sdo_seq = 0;
    int32_t initial_position = 0;
    nanoseconds start_time = kickcat::since_epoch();

    std::shared_ptr<mailbox::request::AbstractMessage> msg = nullptr;

    constexpr int64_t LOOP_NUMBER = 12 * 3600 * 1000; // 12h
    //constexpr int64_t LOOP_NUMBER = 1000 * 60 * 5; // 5min
    int64_t last_error = 0;
    for (int64_t i = 0; i < LOOP_NUMBER; ++i)
    {
        probe.log();
        try
        {
            if (i == 500)
            {
                output_pdo->target_position = input_pdo->actual_position;
                state_machine.setCommand(CANOpenCommand::ENABLE);
            }

            if (i == 4000)
            {
                initial_position = input_pdo->actual_position;
                start_time = kickcat::since_epoch();
            }

            if (i > 4000)
            {
                double time = std::chrono::duration_cast<seconds_f>(kickcat::elapsed_time(start_time)).count();

                constexpr double MOTION_FQ = 0.2; // Hz
                constexpr double MOTION_AMPLITUDE = 80.0 / 180.0 * M_PI;
                constexpr double REDUCTION_RATIO = 100.0;
                constexpr double ENCODER_TICKS_PER_TURN = 1<<23;

                double targetSI = MOTION_AMPLITUDE * std::sin(2 * M_PI * MOTION_FQ * time);

                // Set target
                double measureSI = (input_pdo->actual_position - initial_position) / ENCODER_TICKS_PER_TURN / REDUCTION_RATIO * 2.0 * M_PI;
                output_pdo->target_position =  initial_position - static_cast<int32_t>(REDUCTION_RATIO * targetSI / 2.0 / M_PI * ENCODER_TICKS_PER_TURN);
                output_pdo->target_torque = 3000;

            }

            bus.sendLogicalRead(callback_error);  // Update inputPDO
            bus.sendLogicalWrite(callback_error); // Update outputPDO
            bus.processAwaitingFrames();
            bus.finalizeDatagrams();

            switch (sdo_seq)
            {
                case 0:
                {
                    bus.sendMailboxesReadChecks(callback_error);
                    sdo_seq = 1;
                    break;
                }
                case 1:
                {
                    bus.sendMailboxesWriteChecks(callback_error);
                    sdo_seq = 2;
                    break;
                }
                case 2:
                {
                    bus.sendReadMessages(callback_error); // Get emergencies
                    sdo_seq = 3;
                    break;
                }
                case 3:
                {
                    bus.sendWriteMessages(callback_error);
                    sdo_seq = 0;
                    break;
                }
            }

            bus.processAwaitingFrames();
            bus.finalizeDatagrams();
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

        state_machine.update(input_pdo->status_word);
        output_pdo->control_word = state_machine.getControlWord();


        // Receive emergencies in case of failure
        if (motor.mailbox.emergencies.size() > 0)
        {
            for (auto const& em : motor.mailbox.emergencies)
            {
                std::cerr << "*~~~ Emergency received @ " << i << " ~~~*" << std::endl;
                std::cerr << registerToError(em.error_register);
                std::cerr << codeToError(em.error_code);
            }
            motor.mailbox.emergencies.resize(0);
        }


        if ((i % 100) == 0)
        {
            /*
            uint16_t status;
            uint32_t size = 2;
            if (msg == nullptr)
            {
                msg = bus.slaves().at(0).mailbox.createSDO(0x6040, 0, false, CoE::SDO::request::UPLOAD, &status, &size, 1s);
            }
            else
            {
                if (msg->status() == mailbox::request::MessageStatus::SUCCESS)
                {
                    printf("--> %x\n", status);
                    msg = nullptr;
                }
                else if (msg->status() != mailbox::request::MessageStatus::RUNNING)
                {
                    printf("error reading SDO %x\n", msg->status());
                    msg = nullptr;
                }
            }
                */


            printf("[%d] %x (%x) position %d to %d\n",
                i, state_machine.getControlWord(), input_pdo->status_word,
                input_pdo->actual_torque, output_pdo->target_position);

        }

        probe.log();
        timer.wait_next_tick();
    }

    return 0;
}
