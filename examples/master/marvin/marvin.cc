#include <iostream>
#include <cstring>

#include "kickcat/Bus.h"
#include "kickcat/Link.h"
#include "kickcat/Prints.h"
#include "kickcat/helpers.h"
#include "kickcat/OS/Timer.h"

#include "CanOpenErrors.h"
#include "CanOpenStateMachine.h"
#include "MarvinProtocol.h"


using namespace kickcat;

int main(int argc, char *argv[])
{
    if (argc != 3 and argc != 2)
    {
        printf("usage redundancy mode : ./test NIC_nominal NIC_redundancy\n");
        printf("usage no redundancy mode : ./test NIC_nominal\n");
        return 1;
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

    uint8_t io_buffer[2048];
    try
    {
        bus.init(100ms);

        //TODO
        // remove enableDC from init()
        // go back in INIT, enableDC, go again in PREOP, continue

        //for (auto& slave: bus.slaves())
        //{
        //    printInfo(slave);
        //    printESC(slave);
        //}



        auto map_pdos = [&](uint8_t id)
        {
            uint8_t zero = 0;
            uint8_t uno = 1;
            uint8_t size = 2;
            auto& slave = bus.slaves().at(id);
            bus.writeSDO(slave, 0x1c12, 0, false, &zero, 1);
                bus.writeSDO(slave, 0x1601, 0, false, &zero, 1);

                    uint32_t tx_mapping[] = {0x60410010, 0x60640020};
                    bus.writeSDO(slave, 0x1601, 1, false, &tx_mapping[0], 4);
                    bus.writeSDO(slave, 0x1601, 2, false, &tx_mapping[1], 4);
                    size =2;
                bus.writeSDO(slave, 0x1601, 0, false, &size, 1);
            bus.writeSDO(slave, 0x1c12, 0, false, &uno, 1);


            bus.writeSDO(slave, 0x1c13, 0, false, &zero, 1);
                bus.writeSDO(slave, 0x1a01, 0, false, &zero, 1);

                    uint32_t rx_mapping[] = {0x60400010, 0x60600010, 0x607A0020};
                    bus.writeSDO(slave, 0x1a01, 1, false, &rx_mapping[0], 4);
                    bus.writeSDO(slave, 0x1a01, 2, false, &rx_mapping[1], 4);
                    bus.writeSDO(slave, 0x1a01, 3, false, &rx_mapping[2], 4);
                    size = 3;

                bus.writeSDO(slave, 0x1a01, 0, false, &size, 1);
            bus.writeSDO(slave, 0x1c13, 0, false, &uno, 1);
        };

        printf("try to map\n");

        //for (int i = 0; i < 7; ++i)
        //{
        //    map_pdos(i);
        //    map_pdos(i);
        //}

        uint8_t mode = 8;
        uint32_t mode_size = 1;
        bus.writeSDO(bus.slaves().at(0), 0x6060, 0, false, &mode, mode_size);

        printf("mapping\n");
        bus.createMapping(io_buffer);

        //bus.enableDC(10ms, 10s);

        printf("Request SAFE OP\n");
        bus.requestState(State::SAFE_OP);
        bus.waitForState(State::SAFE_OP, 100ms);
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

    Slave &motor = bus.slaves().at(0);

    pdo::Output *output_pdo = reinterpret_cast<pdo::Output *>(motor.output.data);
    pdo::Input *input_pdo = reinterpret_cast<pdo::Input *>(motor.input.data);

    Timer timer{1ms};
    timer.start();

    for (int i = 0; i < 10; ++i)
    {
        bus.processDataRead(false_alarm);
        bus.processDataWrite(false_alarm);

        timer.wait_next_tick();
    }

    try
    {
        //bool not_ready = true;
        //while(not_ready)
        //{
        //    try
        //    {
        //        not_ready = false;
        //        bus.sendLogicalReadWrite(callback_error);
        //        //bus.processDataRead(callback_error);
//
        //    }
        //    catch (kickcat::ErrorDatagram const& e)
        //    {
        //        not_ready = true;
        //        //std::cerr << e.what() << ": " << toString(e.state()) << std::endl;
//
        //        uint64_t value;
        //        bus.read_address<uint64_t>(reg::DC_START_TIME, value);
        //        bus.read_address<uint64_t>(reg::DC_SYSTEM_TIME, value);
        //    }
//
        //    output_pdo->target_position = input_pdo->actual_position;
        //    output_pdo->control_word = 0x8000;
        //    //bus.processDataWrite(false_alarm);
//
        //    timer.wait_next_tick();
        //}
    }
    catch (...)
    {
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

    CANOpenStateMachine state_machine;

    printf("mapping: input(%d) output(%d)\n", motor.input.bsize, motor.output.bsize);

    state_machine.setCommand(CANOpenCommand::DISABLE);

    // position mode
    //output_pdo->control_word = 6;
    //output_pdo->mode_of_operation = 8;
    //output_pdo->target_position = input_pdo->actual_position;


    std::shared_ptr<mailbox::request::AbstractMessage> msg = nullptr;

    constexpr int64_t LOOP_NUMBER = 12 * 3600 * 1000; // 12h
    int64_t last_error = 0;
    for (int64_t i = 0; i < LOOP_NUMBER; ++i)
    {
        timer.wait_next_tick();


        try
        {
/*
            if (i == 1000)
            {
                printf("INIT\n");
                bus.requestState(State::INIT);
            }
            if (i == 3000)
            {
                printf("PREOP\n");
                bus.waitForState(State::INIT, 2ms);
                bus.requestState(State::PRE_OP);
            }
            if (i == 5000)
            {
                printf("SAFEOP\n");
                bus.waitForState(State::PRE_OP, 2ms);
                bus.requestState(State::SAFE_OP);
            }
            if (i == 7000)
            {
                printf("OP\n");
                bus.waitForState(State::SAFE_OP, 2ms);
                bus.requestState(State::OPERATIONAL);
            }
                */

            if (i == 1000)
            {
                output_pdo->target_position = input_pdo->actual_position;
                state_machine.setCommand(CANOpenCommand::ENABLE);
            }

            bus.sendLogicalRead(false_alarm);  // Update inputPDO
            bus.sendLogicalWrite(false_alarm); // Update outputPDO
            bus.processAwaitingFrames();
            bus.finalizeDatagrams();

            //bus.sendLogicalReadWrite(callback_error);

            bus.processAwaitingFrames();
            bus.finalizeDatagrams();

            bus.sendMailboxesReadChecks(callback_error);
            bus.sendMailboxesWriteChecks(callback_error);
            bus.sendReadMessages(callback_error); // Get emergencies
            bus.sendWriteMessages(callback_error);

            bus.processAwaitingFrames();
            bus.finalizeDatagrams();
/*
            int32_t status;
            uint32_t size = 4;
            if (msg == nullptr)
            {
                msg = motor.mailbox.createSDO(0x6064, 0, false, CoE::SDO::request::UPLOAD, &status, &size, 1s);
            }
            else
            {
                if (msg->status() == mailbox::request::MessageStatus::SUCCESS)
                {
                    printf("ouiiiiiii\n");
                    msg = nullptr;
                }
                else if (msg->status() != mailbox::request::MessageStatus::RUNNING)
                {
                    printf("merde %x\n", msg->status());
                    status = 3;
                    msg = nullptr;
                }
            }
            printf("--> %d\n", status);
*/

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
        state_machine.update(input_pdo->status_word);
        output_pdo->control_word = state_machine.getControlWord();

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

        if (i % 100)
        {

        //bus.read_address<uint8_t>(reg::DC_SYNC_ACTIVATION);
        //bus.read_address<uint32_t>(reg::DC_SYNC0_CYCLE_TIME);
        //uint8_t value_u8;
        //uint64_t value_u64;
        //bus.read_address<uint8_t>(reg::DC_ACTIVATION_STATUS, value_u8);
        //bus.read_address<uint8_t>(reg::DC_SYNC0_STATUS, value_u8);
        //bus.read_address<uint64_t>(reg::DC_START_TIME, value_u64);
        //bus.read_address<uint64_t>(reg::DC_SYSTEM_TIME, value_u64);
        //bus.read_address<uint16_t>(reg::AL_STATUS);
        //bus.read_address<uint32_t>(reg::AL_STATUS_CODE);
        //bus.read_address<uint8_t>(reg::SYNC_MANAGER_2 + 5);
        //bus.read_address<uint8_t>(reg::SYNC_MANAGER_3 + 5, value_u8);

            printf("[%d] {%x} %x (%x) position %d to %d\n",
                i, input_pdo->error_code,
                state_machine.getControlWord(), input_pdo->status_word,
                input_pdo->actual_position, output_pdo->target_position);
            //printf("velocity %d\n", input_pdo->actual_velocity);
            //printf("torque   %d\n", input_pdo->actual_torque);
            //printf("LTor     %d\n", input_pdo->LTor_feedback);
            //printf("temp     %d\n", input_pdo->motor_temperature);
            //printf("RECD     %d\n", input_pdo->RECD);
        }
    }

    return 0;
}
