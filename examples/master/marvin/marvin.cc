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

constexpr double REDUCTION_RATIO[] = {120.0, 120.0, 100.0, 100.0, 100.0, 100.0, 100.0,
                                        120.0, 120.0, 100.0, 100.0, 100.0, 100.0, 100.0};
constexpr double ENCODER_TICKS_PER_TURN[] = {1<<20, 1<<20, 1<<19, 1<<19, 1<<19, 1<<19, 1<<19,
                                            1<<20, 1<<20, 1<<19, 1<<19, 1<<19, 1<<19, 1<<19};

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
    nanoseconds sync_point = 0ns;

    struct Motor
    {
        Slave* slave{};
        pdo::Output* output{};
        pdo::Input*  input{};
        CoE::CiA::DS402::StateMachine state_machine{};
        int32_t initial_position{};
        double reduction_ratio;
        double encoder_ticks_per_turn;
        uint16_t prev_status_word{0xFFFF};
        uint16_t prev_control_word{0xFFFF};
    };
    std::vector<Motor> motors;
    motors.resize(14);

    uint8_t io_buffer[2048] = {};
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

        for (int i = 0; i < 7; ++i)
        {
            motors[i].slave = &bus.slaves().at(i + 1);
            motors[i].encoder_ticks_per_turn = ENCODER_TICKS_PER_TURN[i];
            motors[i].reduction_ratio = REDUCTION_RATIO[i];
        }
        for (int i = 7; i < 14; ++i)
        {
            motors[i].slave = &bus.slaves().at(i + 2); // skip first and last of first arm
            motors[i].encoder_ticks_per_turn = ENCODER_TICKS_PER_TURN[i];
            motors[i].reduction_ratio = REDUCTION_RATIO[i];
        }

        for (auto& motor : motors)
        {
            mapPDO(bus, *motor.slave, 0x1A01, pdo::tx_mapping, pdo::tx_mapping_count, 0x1C13);
            mapPDO(bus, *motor.slave, 0x1601, pdo::rx_mapping, pdo::rx_mapping_count, 0x1C12);
        }

        printf("mapping\n");
        bus.createMapping(io_buffer);

        for (auto& motor : motors)
        {
            motor.output = reinterpret_cast<pdo::Output *>(motor.slave->output.data);
            motor.input  = reinterpret_cast<pdo::Input *> (motor.slave->input.data);
            motor.state_machine.disable();

            uint8_t mode = 8;   //CSP
            uint32_t mode_size = 1;
            bus.writeSDO(*motor.slave, 0x6060, 0, Bus::Access::PARTIAL, &mode, mode_size);

            uint8_t interpolation_value = 1;
            uint32_t interpolation_value_size = 1;
            bus.writeSDO(*motor.slave, 0x60C2, 1, Bus::Access::PARTIAL, &interpolation_value, interpolation_value_size);

            int8_t interpolation_index = -3; // 10^(-3) = milliseconds
            uint32_t interpolation_index_size = 1;
            bus.writeSDO(*motor.slave, 0x60C2, 2, Bus::Access::PARTIAL, &interpolation_index, interpolation_index_size);
        }

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

    nanoseconds start_time = kickcat::since_epoch();
    constexpr int64_t LOOP_NUMBER = 12 * 3600 * 1000; // 12h
    //constexpr int64_t LOOP_NUMBER = 1000 * 60 * 5; // 5min
    int64_t last_error = 0;
    for (int64_t i = 0; i < LOOP_NUMBER; ++i)
    {
        try
        {
            if (i == 500)
            {
                for (auto& motor : motors)
                {
                    motor.output->target_position = motor.input->actual_position;
                    motor.state_machine.enable();
                }
            }

            if (i == 4000)
            {
                for (auto& motor : motors)
                {
                    motor.initial_position = motor.input->actual_position;
                }
                start_time = kickcat::since_epoch();
            }

            if (i > 4000)
            {
                double time = std::chrono::duration_cast<seconds_f>(kickcat::elapsed_time(start_time)).count();

                constexpr double MOTION_FQ = 0.2; // Hz
                constexpr double MOTION_AMPLITUDE = 8.0 / 180.0 * M_PI;
                double targetSI = MOTION_AMPLITUDE * std::sin(2 * M_PI * MOTION_FQ * time);

                // Set target
                for (auto& motor : motors)
                {
                    //double measureSI = (input_pdo[j]->actual_position - initial_position[j]) / ENCODER_TICKS_PER_TURN[j] / REDUCTION_RATIO[j] * 2.0 * M_PI;
                    motor.output->target_position = motor.initial_position - static_cast<int32_t>(motor.reduction_ratio * targetSI / 2.0 / M_PI * motor.encoder_ticks_per_turn);
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

        bool changed = false;
        for (auto& motor : motors)
        {
            auto x = motor.input->status_word;
            motor.state_machine.update(x);
            motor.output->control_word = motor.state_machine.controlWord();
            if (x != motor.prev_status_word or motor.state_machine.controlWord() != motor.prev_control_word)
            {
                changed = true;
            }
            motor.prev_status_word = x;
            motor.prev_control_word = motor.state_machine.controlWord();
        }
        if (changed)
        {
            for (auto const& motor : motors)
            {
                printf("%x (%x) - ", motor.prev_status_word, motor.prev_control_word);
            }
            printf("\n");

            for (size_t m = 0; m < motors.size(); ++m)
            {
                uint16_t sw = motors[m].prev_status_word;
                if ((sw & 0x08) == 0x08 && motors[m].input->error_code != 0)
                {
                    printf("  Motor %zu fault: error_code=0x%04x\n", m, motors[m].input->error_code);
                }
            }
        }

        if ((i % 1000) == 0)
        {
            bus.isDCSynchronized(1000ns);
        }

        timer.wait_next_tick();
    }

    return 0;
}
