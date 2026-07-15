#include <iostream>
#include <memory>
#define _USE_MATH_DEFINES // needed for M_PI on Windows
#include <cmath>
#include <vector>

#include "kickcat/Bus.h"
#include "kickcat/Link.h"
#include "kickcat/Prints.h"
#include "kickcat/helpers.h"
#include "kickcat/OS/Timer.h"
#include "kickcat/CoE/CiA/DS402/Drive.h"

using namespace kickcat;
using CoE::CiA::DS402::Drive;

constexpr double REDUCTION_RATIO[] = {120.0, 120.0, 100.0, 100.0, 100.0, 100.0, 100.0,
                                        120.0, 120.0, 100.0, 100.0, 100.0, 100.0, 100.0};
constexpr double ENCODER_TICKS_PER_TURN[] = {1<<20, 1<<20, 1<<19, 1<<19, 1<<19, 1<<19, 1<<19,
                                            1<<20, 1<<20, 1<<19, 1<<19, 1<<19, 1<<19, 1<<19};
constexpr int MOTOR_COUNT = 14;

int main(int argc, char *argv[])
{
    if (argc != 3 and argc != 2)
    {
        printf("usage redundancy mode : ./marvin NIC_nominal NIC_redundancy\n");
        printf("usage no redundancy mode : ./marvin NIC_nominal\n");
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
        std::unique_ptr<Drive> drive;
        int32_t  initial_position{};
        double   reduction_ratio{};
        double   encoder_ticks_per_turn{};
        uint16_t prev_status_word{0xFFFF};
        uint16_t prev_control_word{0xFFFF};
    };
    std::vector<Motor> motors;
    motors.reserve(MOTOR_COUNT);

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

        for (int i = 0; i < MOTOR_COUNT; ++i)
        {
            Slave& slave = bus.slaves().at(i + 1); // slave 0 is the port junction
            printf("yay %s\n", slave.name().c_str());

            auto drive = std::make_unique<Drive>(bus, slave);
            // This drive uses PDO objects 0x1601/0x1A01 and only accepts the INT8
            // mode entry mapped as a 16-bit word (WidenObject), not a dummy pad.
            drive->configure(CoE::CiA::DS402::control::POSITION_CYCLIC,
                             0x1601, 0x1A01, Drive::PaddingStyle::WidenObject);

            motors.push_back(Motor{std::move(drive), 0, REDUCTION_RATIO[i], ENCODER_TICKS_PER_TURN[i],
                                   0xFFFF, 0xFFFF});
        }

        printf("mapping\n");
        bus.createMapping(io_buffer, sizeof(io_buffer));

        printf("Request SAFE OP\n");
        bus.requestState(State::SAFE_OP);
        try
        {
            bus.waitForState(State::SAFE_OP, 100ms);
        }
        catch (std::exception const& e)
        {
            fprintf(stderr, "SAFE_OP refused: %s\n", e.what());
            fprintf(stderr, "Per-slave state after SAFE_OP request:\n");
            for (auto& slave : bus.slaves())
            {
                try
                {
                    State state = bus.getCurrentState(slave);
                    fprintf(stderr, "  %-20s @0x%04x: %s\n",
                        slave.name().c_str(), slave.address, toString(state));
                }
                catch (ErrorAL const& slave_error)
                {
                    fprintf(stderr, "  %-20s @0x%04x: REFUSED, AL status 0x%04x (%s)\n",
                        slave.name().c_str(), slave.address,
                        slave_error.code(), ALStatus_to_string(slave_error.code()));
                }
            }
            throw;
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

    auto false_alarm = [](DatagramState const &) {};

    Timer timer{1ms};
    timer.start(sync_point);

    for (int i = 0; i < 10; ++i)
    {
        bus.processDataRead(false_alarm);
        bus.processDataWrite(false_alarm);

        timer.wait_next_tick();
    }

    // Input image is valid now: capture typed PDO pointers and seed the setpoints.
    // Force mode 0 until enabling at i==500 (attach() seeds the configured mode 8);
    // the RxPDO mode byte overrides the SDO-set mode every cycle.
    for (auto& motor : motors)
    {
        motor.drive->attach();
        motor.drive->setModeOfOperationRaw(0);
        motor.drive->disable();
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
            if (i < 500)
            {
                for (auto& motor : motors)
                {
                    motor.drive->setTargetPositionRaw(motor.drive->actualPositionRaw());
                }
            }

            if (i == 500)
            {
                for (auto& motor : motors)
                {
                    motor.drive->setModeOfOperationRaw(8); // CSP
                    motor.drive->enable();
                }
            }

            if (i == 4000)
            {
                for (auto& motor : motors)
                {
                    motor.initial_position = motor.drive->actualPositionRaw();
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
                    motor.drive->setTargetPositionRaw(motor.initial_position - static_cast<int32_t>(motor.reduction_ratio * targetSI / 2.0 / M_PI * motor.encoder_ticks_per_turn));
                }
            }


            bus.sendLogicalRead(false_alarm);  // Update inputPDO
            bus.sendLogicalWrite(false_alarm); // Update outputPDO
            bus.processAwaitingFrames();
            bus.finalizeDatagrams();

            if ((i % 1000) == 0)
            {
                bus.isDCSynchronized(10us);
            }
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

        // State (0x6F) + warning (0x80). Ignore the bits that toggle every cycle
        // during motion (bit10 target-reached, bit12/13 mode-specific) so they
        // don't spam the trace while the drive is operating normally.
        constexpr uint16_t STATUS_SIGNIFICANT = 0x6f | 0x80;
        bool changed = false;
        for (auto& motor : motors)
        {
            uint16_t status = motor.drive->statusWord();
            motor.drive->update();
            uint16_t control_word = motor.drive->controlWord();
            if (((status ^ motor.prev_status_word) & STATUS_SIGNIFICANT) != 0
                or control_word != motor.prev_control_word)
            {
                changed = true;
            }
            motor.prev_status_word = status;
            motor.prev_control_word = control_word;
        }
        if (changed)
        {
            //for (auto const& motor : motors)
            //{
            //    printf("%x (%x) - ", motor.prev_status_word, motor.prev_control_word);
            //}
            //printf("\n");

            for (size_t m = 0; m < motors.size(); ++m)
            {
                // DS402 Fault state: (statusword & 0x4F) == 0x08. Masking bit3
                // alone false-triggers when bit3 rides on an enabled statusword.
                uint16_t sw = motors[m].prev_status_word;
                if ((sw & 0x4f) == 0x08)
                {
                    printf("  Motor %zu FAULT: status=0x%04x error_code=0x%04x\n",
                        m, sw, motors[m].drive->errorCode());
                }
            }
        }

        timer.wait_next_tick();
    }

    return 0;
}
