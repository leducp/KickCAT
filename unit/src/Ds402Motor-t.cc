// Integration tests for the simulated CiA-402 motor (kickcat::sim::Ds402Motor):
// a real master Bus and DS402 Drive drive an emulated slave built from a DS402 ESI
// over the in-process loopback, so the motor is observed only through its PDO image.
#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "mocks/Time.h"

#include "kickcat/Bus.h"
#include "kickcat/CoE/CiA/DS402/Drive.h"
#include "kickcat/Link.h"
#include "kickcat/OS/Filesystem.h"
#include "kickcat/LoopbackSocket.h"
#include "kickcat/Slave.h"
#include "kickcat/SocketNull.h"
#include "kickcat/simulation/SimulatedSlave.h"

using namespace kickcat;
using namespace kickcat::CoE::CiA::DS402;


namespace
{
    // Guard: no loop below may run away. Kept tight on purpose - every now() call advances the
    // mock clock 1 ms, and once it passes real monotonic uptime the OS waits behind Timer and
    // ConditionVariable stop returning immediately (see lib/src/OS/Test/Time.cc). The loops here
    // converge in under 60 cycles.
    constexpr int MAX_CYCLES = 300;

    // Statuswords the emulated motor exposes for the CiA-402 states the master walks through.
    constexpr uint16_t SW_SWITCH_ON_DISABLED = status::masks::SWITCH_ON_DISABLED;
    constexpr uint16_t SW_READY              = status::masks::READY_TO_SWITCH_ON
                                             | status::masks::VOLTAGE_ENABLED
                                             | status::masks::QUICK_STOP;
    constexpr uint16_t SW_OPERATION_ENABLED  = SW_READY
                                             | status::masks::SWITCHED_ON
                                             | status::masks::OPERATION_ENABLE;



    class Ds402MotorTest : public testing::Test
    {
    protected:
        void TearDown() override
        {
            resetMockClock(); // do not leak burnt mock time into the suites that follow
            if (not temp_config_.empty())
            {
                filesystem::removeFile(temp_config_);
            }
        }

        // A config in a temporary directory, so plant parameters can be tuned per test.
        // The ESI is referenced absolutely: buildSlave resolves it against the config's directory.
        std::string writeConfig(std::string const& name, std::string const& extra_params)
        {
            temp_config_ = name; // beside the binary, next to the fixture its esi refers to
            filesystem::writeFile(temp_config_,
                "{\"esi\": \"ecat402-drive.xml\", \"ds402_motor\": true" + extra_params + "}");
            return temp_config_;
        }

        void bringToOperational(std::string const& config, control::ControlMode mode)
        {
            resetMockClock();
            iomap_.assign(4096, 0);

            sim_ = std::make_unique<sim::SimulatedSlave>(sim::buildSlave(config));
            ASSERT_NE(sim_->device, nullptr);
            sim_->slave->start();

            auto tick = [this]()
            {
                sim_->slave->routine();
                if (op_phase_ and sim_->slave->state() == State::SAFE_OP)
                {
                    sim_->slave->validateOutputData();
                }
                sim_->device->step();
            };

            loopback_ = std::make_shared<LoopbackSocket>(std::vector<EmulatedESC*>{sim_->esc.get()}, tick);
            link_     = std::make_shared<Link>(loopback_, std::make_shared<SocketNull>(), [](){});
            bus_      = std::make_unique<Bus>(link_);

            bus_->init(100ms);
            ASSERT_EQ(bus_->slaves().size(), 1u);

            drive_ = std::make_unique<Drive>(*bus_, bus_->slaves().at(0));
            drive_->configure(mode, 0x1600, 0x1A00, Drive::PaddingStyle::Auto);
            bus_->createMapping(iomap_.data(), iomap_.size());
            drive_->attach();

            bus_->requestState(State::SAFE_OP);
            bus_->waitForState(State::SAFE_OP, 500ms);
            cycle();

            op_phase_ = true;
            bus_->requestState(State::OPERATIONAL);
            bus_->waitForState(State::OPERATIONAL, 500ms, [this]() { cycle(); });
            ASSERT_EQ(sim_->slave->state(), State::OPERATIONAL);
        }

        void cycle()
        {
            auto noop = [](DatagramState const&) {};
            bus_->processDataRead(noop);
            drive_->update();
            bus_->processDataWrite(noop);
        }

        void enableDrive()
        {
            drive_->enable();
            int cycles = 0;
            while (not drive_->isEnabled() and cycles < MAX_CYCLES)
            {
                cycle();
                ++cycles;
            }
            ASSERT_TRUE(drive_->isEnabled());
        }

        void run(int cycles)
        {
            for (int i = 0; i < cycles; ++i)
            {
                cycle();
            }
        }

        std::vector<uint8_t>                 iomap_;
        std::unique_ptr<sim::SimulatedSlave> sim_;
        std::shared_ptr<LoopbackSocket>      loopback_;
        std::shared_ptr<Link>                link_;
        std::unique_ptr<Bus>                 bus_;
        std::unique_ptr<Drive>               drive_;
        bool                                 op_phase_{false};
        std::string                          temp_config_;
    };
}

TEST_F(Ds402MotorTest, master_enable_sequence_walks_the_motor_to_operation_enabled)
{
    ASSERT_NO_FATAL_FAILURE(bringToOperational(std::string{"ecat402-drive.json"}, control::VELOCITY_CYCLIC));

    EXPECT_EQ(drive_->statusWord(), SW_SWITCH_ON_DISABLED);
    EXPECT_EQ(drive_->modeOfOperationDisplay(), control::VELOCITY_CYCLIC);

    std::vector<uint16_t> sequence;
    drive_->enable();
    int cycles = 0;
    while (not drive_->isEnabled() and cycles < MAX_CYCLES)
    {
        cycle();
        if (sequence.empty() or sequence.back() != drive_->statusWord())
        {
            sequence.push_back(drive_->statusWord());
        }
        ++cycles;
    }

    ASSERT_TRUE(drive_->isEnabled());
    EXPECT_FALSE(drive_->isFaulted());
    // The master's state machine jumps from SHUTDOWN straight to ENABLE_OPERATION, so
    // SWITCHED-ON is never commanded and never observed.
    EXPECT_EQ(sequence, std::vector<uint16_t>({SW_SWITCH_ON_DISABLED, SW_READY, SW_OPERATION_ENABLED}));
    EXPECT_EQ(drive_->statusWord(), SW_OPERATION_ENABLED);
}

TEST_F(Ds402MotorTest, cyclic_velocity_follows_the_commanded_direction)
{
    ASSERT_NO_FATAL_FAILURE(bringToOperational(std::string{"ecat402-drive.json"}, control::VELOCITY_CYCLIC));
    ASSERT_NO_FATAL_FAILURE(enableDrive());

    constexpr int32_t TARGET = 50000;  // ticks/s
    int32_t const start = drive_->actualPositionRaw();
    drive_->setTargetVelocityRaw(TARGET);
    run(100);  // vel_tau is 10 ms at a 1 ms plant cycle: the velocity loop has settled

    EXPECT_GT(drive_->actualVelocityRaw(), TARGET / 2);
    EXPECT_LE(drive_->actualVelocityRaw(), TARGET);
    EXPECT_GT(drive_->actualPositionRaw(), start);

    int32_t previous = drive_->actualPositionRaw();
    for (int i = 0; i < 50; ++i)
    {
        cycle();
        EXPECT_GT(drive_->actualPositionRaw(), previous);
        previous = drive_->actualPositionRaw();
    }

    drive_->setTargetVelocityRaw(-TARGET);
    run(100);

    EXPECT_LT(drive_->actualVelocityRaw(), -TARGET / 2);
    EXPECT_GE(drive_->actualVelocityRaw(), -TARGET);

    previous = drive_->actualPositionRaw();
    for (int i = 0; i < 50; ++i)
    {
        cycle();
        EXPECT_LT(drive_->actualPositionRaw(), previous);
        previous = drive_->actualPositionRaw();
    }
}

TEST_F(Ds402MotorTest, cyclic_position_converges_to_the_commanded_target)
{
    ASSERT_NO_FATAL_FAILURE(bringToOperational(std::string{"ecat402-drive.json"}, control::POSITION_CYCLIC));
    ASSERT_NO_FATAL_FAILURE(enableDrive());

    constexpr int32_t STEP = 100000;  // ticks
    int32_t const target = drive_->actualPositionRaw() + STEP;
    drive_->setTargetPositionRaw(target);

    run(20);
    int64_t const early_error = std::abs(static_cast<int64_t>(target) - drive_->actualPositionRaw());
    EXPECT_LT(early_error, STEP);  // the plant lags, but it is already closing the gap

    int cycles = 0;
    int64_t error = early_error;
    while (error > STEP / 100 and cycles < MAX_CYCLES)
    {
        cycle();
        error = std::abs(static_cast<int64_t>(target) - drive_->actualPositionRaw());
        ++cycles;
    }

    EXPECT_LE(error, STEP / 100);
    EXPECT_LT(cycles, MAX_CYCLES);
}

// Regression: the plant state is a double and outgrows the INT32 feedback objects. It
// must clamp on the way into the PDO, not wrap (nor trap on the float-to-int cast).
TEST_F(Ds402MotorTest, position_feedback_saturates_instead_of_wrapping)
{
    // A 10 ms plant cycle equal to vel_tau makes the velocity loop reach its target in one
    // step, so the position crosses the INT32 range in a few hundred cycles.
    std::string config = writeConfig("kickcat_ds402_saturation-t.json", ", \"motor_cycle_ms\": 10.0");
    ASSERT_NO_FATAL_FAILURE(bringToOperational(config, control::VELOCITY_CYCLIC));
    ASSERT_NO_FATAL_FAILURE(enableDrive());

    drive_->setTargetVelocityRaw(INT32_MAX);

    int32_t previous = drive_->actualPositionRaw();
    int cycles = 0;
    while (drive_->actualPositionRaw() != INT32_MAX and cycles < MAX_CYCLES)
    {
        cycle();
        ASSERT_GE(drive_->actualPositionRaw(), previous) << "position wrapped at cycle " << cycles;
        previous = drive_->actualPositionRaw();
        ++cycles;
    }

    EXPECT_GT(cycles, 0);  // the clamp must have been reached by moving, not from the start
    EXPECT_EQ(drive_->actualPositionRaw(), INT32_MAX);
    EXPECT_EQ(drive_->actualVelocityRaw(), INT32_MAX);
    EXPECT_EQ(drive_->actualTorqueRaw(), INT16_MAX);

    // Position holds at the clamp instead of rolling over on the next cycles.
    run(20);
    EXPECT_EQ(drive_->actualPositionRaw(), INT32_MAX);
}
