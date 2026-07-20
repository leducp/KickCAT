#include <gtest/gtest.h>
#include <cstring>

#include "mocks/Link.h"
#include "mocks/Time.h"

#include "kickcat/Bus.h"
#include "kickcat/Error.h"
#include "kickcat/Slave.h"
#include "kickcat/Units.h"
#include "kickcat/CoE/CiA/DS402/Drive.h"

using namespace kickcat;
using namespace kickcat::CoE::CiA::DS402;

class DS402DriveTest : public testing::Test
{
protected:
    std::shared_ptr<MockLink> mock_link{ std::make_shared<MockLink>() };
    Bus bus{ mock_link };
    Slave slave{};
    Drive drive{bus, slave};

    Drive::Input  tx{};
    Drive::Output rx{};

    void SetUp() override
    {
        resetMockClock();

        slave.output.data = reinterpret_cast<uint8_t*>(&rx);
        slave.input.data  = reinterpret_cast<uint8_t*>(&tx);
        drive.attach();
    }

    void advanceClock(milliseconds duration)
    {
        for (int i = 0; i < duration.count(); ++i)
        {
            now();
        }
    }
};

// --- attach ---

TEST(DS402DriveAttachTest, attach_throws_if_pdo_buffers_unset)
{
    auto mock_link = std::make_shared<MockLink>();
    Bus bus{ mock_link };
    Slave slave{};
    Drive drive{bus, slave};

    EXPECT_THROW(drive.attach(), kickcat::Error);
}

TEST(DS402DriveAttachTest, attach_initializes_target_position_to_actual_position)
{
    auto mock_link = std::make_shared<MockLink>();
    Bus bus{ mock_link };
    Slave slave{};
    Drive drive{bus, slave};

    Drive::Input  tx{};
    Drive::Output rx{};
    tx.actual_position = 123456;
    rx.target_position = 0xDEADBEEF;  // junk that must be overwritten

    slave.output.data = reinterpret_cast<uint8_t*>(&rx);
    slave.input.data  = reinterpret_cast<uint8_t*>(&tx);
    drive.attach();

    EXPECT_EQ(rx.target_position, 123456);
    EXPECT_EQ(rx.control_word,    0);
    EXPECT_EQ(rx.target_velocity, 0);
    EXPECT_EQ(rx.target_torque,   0);
}

// --- accessors ---

TEST_F(DS402DriveTest, statusword_reads_slave_input_buffer)
{
    tx.status_word = 0x1234;
    EXPECT_EQ(drive.statusWord(), 0x1234);
}

TEST_F(DS402DriveTest, controlword_reads_slave_output_buffer)
{
    rx.control_word = 0x5678;
    EXPECT_EQ(drive.controlWord(), 0x5678);
}

TEST_F(DS402DriveTest, mode_of_operation_display_reads_input)
{
    tx.mode_of_operation_display = control::TORQUE;
    EXPECT_EQ(drive.modeOfOperationDisplay(), control::TORQUE);
}

TEST_F(DS402DriveTest, error_code_reads_input)
{
    tx.error_code = 0x7305;
    EXPECT_EQ(drive.errorCode(), 0x7305);
}

TEST_F(DS402DriveTest, set_mode_of_operation_raw_writes_output)
{
    drive.setModeOfOperationRaw(control::POSITION_CYCLIC);
    EXPECT_EQ(rx.mode_of_operation, control::POSITION_CYCLIC);
}

TEST_F(DS402DriveTest, raw_setpoints_write_output_buffer)
{
    drive.setTargetPositionRaw(12345);
    drive.setTargetVelocityRaw(-6789);
    drive.setTargetTorqueRaw(100);

    EXPECT_EQ(rx.target_position, 12345);
    EXPECT_EQ(rx.target_velocity, -6789);
    EXPECT_EQ(rx.target_torque,   100);
}

TEST_F(DS402DriveTest, raw_actuals_read_input_buffer)
{
    tx.actual_position = 4242;
    tx.actual_velocity = -100;
    tx.actual_torque   = 50;

    EXPECT_EQ(drive.actualPositionRaw(), 4242);
    EXPECT_EQ(drive.actualVelocityRaw(), -100);
    EXPECT_EQ(drive.actualTorqueRaw(),   50);
}

// --- update() ---

TEST_F(DS402DriveTest, update_with_no_command_writes_zero_controlword)
{
    tx.status_word = 0;
    drive.update();
    EXPECT_EQ(rx.control_word, 0);
    EXPECT_FALSE(drive.isEnabled());
}

TEST_F(DS402DriveTest, update_detects_fault_from_status)
{
    tx.status_word = status::value::FAULT_STATE;
    drive.update();
    EXPECT_TRUE(drive.isFaulted());
}

TEST_F(DS402DriveTest, update_drives_full_enable_sequence)
{
    drive.enable();

    tx.status_word = 0;
    drive.update();

    drive.update();
    EXPECT_EQ(rx.control_word, control::word::SHUTDOWN);

    advanceClock(110ms);
    drive.update();
    EXPECT_EQ(rx.control_word, control::word::SHUTDOWN);

    tx.status_word = status::value::READY_TO_SWITCH_ON_STATE;
    drive.update();

    tx.status_word = status::value::ON_STATE;
    drive.update();
    EXPECT_EQ(rx.control_word, control::word::ENABLE_OPERATION);
    EXPECT_TRUE(drive.isEnabled());
}

TEST_F(DS402DriveTest, update_disable_writes_disable_voltage)
{
    drive.disable();
    drive.update();
    EXPECT_EQ(rx.control_word, control::word::DISABLE_VOLTAGE);
}

// --- setUnits validation ---

TEST_F(DS402DriveTest, set_units_rejects_zero_encoder_ticks)
{
    EXPECT_THROW(drive.setUnits({0.0, 1.0, 1.0}), kickcat::Error);
}

TEST_F(DS402DriveTest, set_units_rejects_zero_gear_ratio)
{
    EXPECT_THROW(drive.setUnits({1.0, 0.0, 1.0}), kickcat::Error);
}

TEST_F(DS402DriveTest, set_units_rejects_zero_rated_torque)
{
    EXPECT_THROW(drive.setUnits({1.0, 1.0, 0.0}), kickcat::Error);
}

TEST_F(DS402DriveTest, set_units_rejects_negative_values)
{
    EXPECT_THROW(drive.setUnits({-1.0, 1.0, 1.0}), kickcat::Error);
    EXPECT_THROW(drive.setUnits({1.0, -1.0, 1.0}), kickcat::Error);
    EXPECT_THROW(drive.setUnits({1.0, 1.0, -1.0}), kickcat::Error);
}

// --- SI position ---

TEST_F(DS402DriveTest, si_position_marvin_units_matches_existing_math)
{
    drive.setUnits({static_cast<double>(1U << 20), 120.0, 1.0});

    double target_rad = 8.0 / 180.0 * tau / 2.0;  // 8 degrees
    drive.setTargetPosition(target_rad);

    int32_t expected = static_cast<int32_t>(
        target_rad * static_cast<double>(1U << 20) * 120.0 / tau);
    EXPECT_EQ(rx.target_position, expected);
}

TEST_F(DS402DriveTest, si_position_roundtrip)
{
    drive.setUnits({static_cast<double>(1U << 19), 100.0, 1.0});

    drive.setTargetPosition(0.5);
    tx.actual_position = rx.target_position;

    EXPECT_NEAR(drive.actualPosition(), 0.5, 1e-4);
}

TEST_F(DS402DriveTest, si_position_zero_writes_zero)
{
    drive.setUnits({static_cast<double>(1U << 20), 120.0, 1.0});
    drive.setTargetPosition(0.0);
    EXPECT_EQ(rx.target_position, 0);
}

TEST_F(DS402DriveTest, si_position_negative_rad_writes_negative_ticks)
{
    drive.setUnits({1024.0, 1.0, 1.0});
    drive.setTargetPosition(-tau);
    EXPECT_EQ(rx.target_position, -1024);
}

TEST_F(DS402DriveTest, si_position_saturates_on_overflow)
{
    drive.setUnits({static_cast<double>(1U << 20), 120.0, 1.0});
    // 1000 rad output * 2^20 * 120 / 2π ≈ 2e10 ticks -- well beyond INT32_MAX.
    drive.setTargetPosition(1000.0);
    EXPECT_EQ(rx.target_position, std::numeric_limits<int32_t>::max());

    drive.setTargetPosition(-1000.0);
    EXPECT_EQ(rx.target_position, std::numeric_limits<int32_t>::min());
}

// --- SI velocity ---

TEST_F(DS402DriveTest, si_velocity_uses_position_factor)
{
    drive.setUnits({1024.0, 1.0, 1.0});
    drive.setTargetVelocity(tau);
    EXPECT_EQ(rx.target_velocity, 1024);
}

TEST_F(DS402DriveTest, si_velocity_roundtrip)
{
    drive.setUnits({static_cast<double>(1U << 18), 50.0, 1.0});

    drive.setTargetVelocity(1.5);
    tx.actual_velocity = rx.target_velocity;

    EXPECT_NEAR(drive.actualVelocity(), 1.5, 1e-4);
}

// --- SI torque ---

TEST_F(DS402DriveTest, si_torque_one_rated_is_thousand_per_mille)
{
    drive.setUnits({1.0, 1.0, 2.5});
    drive.setTargetTorque(2.5);
    EXPECT_EQ(rx.target_torque, 1000);
}

TEST_F(DS402DriveTest, si_torque_fraction)
{
    drive.setUnits({1.0, 1.0, 10.0});
    drive.setTargetTorque(1.0);
    EXPECT_EQ(rx.target_torque, 100);
}

TEST_F(DS402DriveTest, si_torque_roundtrip)
{
    drive.setUnits({1.0, 1.0, 3.7});

    drive.setTargetTorque(1.85);
    tx.actual_torque = rx.target_torque;

    EXPECT_NEAR(drive.actualTorque(), 1.85, 1e-2);
}

TEST_F(DS402DriveTest, si_torque_saturates_on_overflow)
{
    drive.setUnits({1.0, 1.0, 0.5});
    // 100 Nm with rated 0.5 -> 200000 per-mille, exceeds INT16_MAX.
    drive.setTargetTorque(100.0);
    EXPECT_EQ(rx.target_torque, std::numeric_limits<int16_t>::max());

    drive.setTargetTorque(-100.0);
    EXPECT_EQ(rx.target_torque, std::numeric_limits<int16_t>::min());
}

// --- destructor ---

TEST(DS402DriveDestructorTest, destructor_writes_disable_voltage_to_rx_pdo)
{
    auto mock_link = std::make_shared<MockLink>();
    Bus bus{ mock_link };
    Slave slave{};

    Drive::Input  tx{};
    Drive::Output rx{};
    tx.actual_position = 78901;     // holds position to prevent a slam
    rx.control_word    = 0xFFFF;
    slave.output.data = reinterpret_cast<uint8_t*>(&rx);
    slave.input.data  = reinterpret_cast<uint8_t*>(&tx);

    {
        Drive drive{bus, slave};
        drive.attach();
    }

    EXPECT_EQ(rx.control_word,    control::word::DISABLE_VOLTAGE);
    EXPECT_EQ(rx.target_position, 78901);
    EXPECT_EQ(rx.target_velocity, 0);
    EXPECT_EQ(rx.target_torque,   0);
}

TEST(DS402DriveDestructorTest, destructor_safe_when_attach_was_not_called)
{
    auto mock_link = std::make_shared<MockLink>();
    Bus bus{ mock_link };
    Slave slave{};

    // Construct and destruct without attach(): must not segfault on null out_.
    { Drive drive{bus, slave}; }
}

// --- canonical PDO layout ---

TEST(DS402DrivePdoLayoutTest, packed_struct_sizes)
{
    EXPECT_EQ(sizeof(Drive::Input),  16U);
    EXPECT_EQ(sizeof(Drive::Output), 14U);
}
