#include <iostream>
#include <cstring>
#define _USE_MATH_DEFINES // needed for M_PI on Windows
#include <cmath>

#include "kickcat/Bus.h"
#include "kickcat/Link.h"
#include "kickcat/Prints.h"
#include "kickcat/helpers.h"
#include "kickcat/OS/Timer.h"
#include "kickcat/CoE/CiA/DS402/StateMachine.h"

#include "kickcat/Diagnostics.h"

#include "CanOpenErrors.h"
#include "MarvinProtocol.h"

//#include <rtm/probe.h>
//#include <rtm/io/posix/local_socket.h>

using namespace kickcat;

int main(int argc, char *argv[])
{
    if (argc != 3 and argc != 2)
    {
        printf("usage redundancy mode : ./test NIC_nominal NIC_redundancy\n");
        printf("usage no redundancy mode : ./test NIC_nominal\n");
        return 1;
    }
/*
    rtm::Probe probe;
    {
        auto io = std::make_unique<rtm::LocalSocket>();
        auto rc = io->open(rtm::access::Mode::READ_WRITE);
        if (rc)
        {
            printf("io open() error: %s\n", rc.message().c_str());
            return 1;
        }

        probe.init("marvin", "op",
                since_epoch(), 1ms, 42,
                std::move(io));
    }
*/
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

    uint8_t io_buffer[2048];
    try
    {
        bus.init(100ms);

        bus.requestState(State::INIT);
        bus.waitForState(State::INIT, 5000ms);

        sync_point = bus.enableDC(1ms, 500us, 100ms);
        printf("now %f, sp: %f -> %f\n",
            seconds_f(since_epoch()).count(),
            seconds_f(sync_point).count(),
            seconds_f(sync_point - since_epoch()).count());

        bus.requestState(State::PRE_OP);
        bus.waitForState(State::PRE_OP, 3000ms);


        /*
        // Map RxPDO/TxPDO
        for (int i = 0; i < 7; ++i)
        {
            mapPDO(bus, bus.slaves().at(i), 0x1A01, pdo::rx_mapping, pdo::rx_mapping_count, 0x1C13);
            mapPDO(bus, bus.slaves().at(i), 0x1601, pdo::tx_mapping, pdo::tx_mapping_count, 0x1C12);
        }
        */


        uint8_t mode = 8;
        uint32_t mode_size = 1;
        for (int j = 1; j < 8; ++j)
        {
            bus.writeSDO(bus.slaves().at(j), 0x6060, 0, Bus::Access::PARTIAL, &mode, mode_size);
        }

        printf("mapping\n");
        bus.createMapping(io_buffer);

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

    constexpr int MOTORS = 14;
    pdo::Output* output_pdo[MOTORS];
    pdo::Input*  input_pdo[MOTORS];
    CoE::CiA::DS402::StateMachine state_machine[MOTORS];
    for (int i = 0; i < 7; ++i)
    {
        Slave& motor = bus.slaves().at(i + 1);
        output_pdo[i] = reinterpret_cast<pdo::Output *>(motor.output.data);
        input_pdo[i] = reinterpret_cast<pdo::Input *>(motor.input.data);
        state_machine[i].disable();
    }
    for (int i = 0; i < 7; ++i)
    {
        Slave& motor = bus.slaves().at(i + 9);
        output_pdo[i+7] = reinterpret_cast<pdo::Output *>(motor.output.data);
        input_pdo[i+7] = reinterpret_cast<pdo::Input *>(motor.input.data);
        state_machine[i+7].disable();
    }

    Timer timer{1ms};
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

    //printf("mapping: input(%d) output(%d)\n", motor.input.bsize, motor.output.bsize);

    int32_t initial_position[MOTORS] = {0};
    nanoseconds start_time = kickcat::since_epoch();

    std::shared_ptr<mailbox::request::AbstractMessage> msg = nullptr;

    constexpr int64_t LOOP_NUMBER = 12 * 3600 * 1000; // 12h
    //constexpr int64_t LOOP_NUMBER = 1000 * 60 * 5; // 5min
    int64_t last_error = 0;
    for (int64_t i = 0; i < LOOP_NUMBER; ++i)
    {
        //probe.log();

        try
        {
            if (i == 500)
            {
                for (int j = 0; j < MOTORS; ++j)
                {
                    output_pdo[j]->target_position = input_pdo[j]->actual_position;
                    state_machine[j].enable();
                }
            }

            if (i == 4000)
            {
                for (int j = 0; j < MOTORS; ++j)
                {
                    initial_position[j] = input_pdo[j]->actual_position;
                }
                start_time = kickcat::since_epoch();
            }

            if (i > 4000)
            {
                double time = std::chrono::duration_cast<seconds_f>(kickcat::elapsed_time(start_time)).count();

                constexpr double MOTION_FQ = 0.2; // Hz
                constexpr double MOTION_AMPLITUDE = 8.0 / 180.0 * M_PI;
                constexpr double REDUCTION_RATIO[] = {120.0, 120.0, 100.0, 100.0, 100.0, 100.0, 100.0, 120.0,
                                                      120.0, 100.0, 100.0, 100.0, 100.0, 100.0};
                constexpr double ENCODER_TICKS_PER_TURN[] = {1<<20, 1<<20, 1<<19, 1<<19, 1<<19, 1<<19, 1<<19,
                                                             1<<20, 1<<20, 1<<19, 1<<19, 1<<19, 1<<19, 1<<19};

                double targetSI = MOTION_AMPLITUDE * std::sin(2 * M_PI * MOTION_FQ * time);

                // Set target
                for (int j = 0; j < MOTORS; ++j)
                {
                    //double measureSI = (input_pdo[j]->actual_position - initial_position[j]) / ENCODER_TICKS_PER_TURN[j] / REDUCTION_RATIO[j] * 2.0 * M_PI;
                    output_pdo[j]->target_position =  initial_position[j] - static_cast<int32_t>(REDUCTION_RATIO[j] * targetSI / 2.0 / M_PI * ENCODER_TICKS_PER_TURN[j]);
                }
            }


            bus.sendLogicalRead(false_alarm);  // Update inputPDO
            bus.sendLogicalWrite(false_alarm); // Update outputPDO
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

        for (int j = 0; j<MOTORS; ++j)
        {
            state_machine[j].update(input_pdo[j]->status_word);
            output_pdo[j]->control_word = state_machine[j].controlWord();
        }

        //if ((i % 100) == 0)
        //{
        //    printf("[%ld] {%x} %x (%x) position %d to %d - RECD %d\n",
        //        i, input_pdo[0]->error_code,
        //        state_machine[0].controlWord(), input_pdo[0]->status_word,
        //        input_pdo[0]->actual_position, output_pdo[0]->target_position,
        //        input_pdo[0]->RECD);
        //}

        if ((i % 1000) == 0)
        {
            bus.isDCSynchronized(1000ns, true);
        }

        //probe.log();

        timer.wait_next_tick();
    }

    return 0;
}
