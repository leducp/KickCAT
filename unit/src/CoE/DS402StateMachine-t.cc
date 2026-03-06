#include <gtest/gtest.h>

#include "mocks/Time.h"
#include "kickcat/CoE/CiA/DS402/StateMachine.h"

using namespace kickcat;
using namespace kickcat::CoE::CiA::DS402;

class DS402StateMachineTest : public testing::Test
{
protected:
    StateMachine sm;

    void SetUp() override
    {
        resetSinceEpoch();
    }

    // Each since_epoch() call advances the mock clock by 1ms.
    void advanceClock(milliseconds duration)
    {
        for (int i = 0; i < duration.count(); ++i)
        {
            since_epoch();
        }
    }

    void reachSafeReset()
    {
        sm.enable();
        sm.update(0);
    }

    void reachPrepareToSwitchOn()
    {
        reachSafeReset();
        advanceClock(110ms);
        sm.update(0);
    }

    void reachSwitchOn()
    {
        reachPrepareToSwitchOn();
        sm.update(status::value::READY_TO_SWITCH_ON_STATE);
    }

    void reachOn()
    {
        reachSwitchOn();
        sm.update(status::value::ON_STATE);
    }
};

// --- Initial state ---

TEST_F(DS402StateMachineTest, initial_control_word_is_zero)
{
    EXPECT_EQ(sm.controlWord(), 0);
}

TEST_F(DS402StateMachineTest, initial_is_not_enabled)
{
    EXPECT_FALSE(sm.isEnabled());
}

TEST_F(DS402StateMachineTest, initial_is_not_faulted)
{
    EXPECT_FALSE(sm.isFaulted());
}

// --- Command::NONE ---

TEST_F(DS402StateMachineTest, no_command_does_nothing)
{
    sm.update(0);
    EXPECT_EQ(sm.controlWord(), 0);

    sm.update(status::value::ON_STATE);
    EXPECT_EQ(sm.controlWord(), 0);
}

TEST_F(DS402StateMachineTest, no_command_detects_fault)
{
    sm.update(status::value::FAULT_STATE);
    EXPECT_TRUE(sm.isFaulted());
}

TEST_F(DS402StateMachineTest, no_command_fault_clears)
{
    sm.update(status::value::FAULT_STATE);
    EXPECT_TRUE(sm.isFaulted());

    sm.update(0);
    EXPECT_FALSE(sm.isFaulted());
}

// --- ENABLE: OFF state, no fault ---

TEST_F(DS402StateMachineTest, enable_from_off_no_fault_goes_to_safe_reset)
{
    sm.enable();
    sm.update(0);

    // Non-faulted: goes directly to SAFE_RESET, next update sends SHUTDOWN
    sm.update(0);
    EXPECT_EQ(sm.controlWord(), control::word::SHUTDOWN);
}

// --- ENABLE: OFF state, with fault ---

TEST_F(DS402StateMachineTest, enable_off_fault_starts_with_fault_reset)
{
    sm.enable();
    sm.update(status::value::FAULT_STATE);

    // Toggle fires immediately on first call (bit 7 high)
    EXPECT_EQ(sm.controlWord(), control::word::FAULT_RESET);
    EXPECT_TRUE(sm.isFaulted());
}

TEST_F(DS402StateMachineTest, enable_off_fault_toggles_after_half_period)
{
    sm.enable();
    sm.update(status::value::FAULT_STATE);
    EXPECT_EQ(sm.controlWord(), control::word::FAULT_RESET);

    // Advance past the 50ms half-period: toggles to 0
    advanceClock(55ms);
    sm.update(status::value::FAULT_STATE);
    EXPECT_EQ(sm.controlWord(), 0);
}

TEST_F(DS402StateMachineTest, enable_off_fault_toggles_back_to_fault_reset)
{
    sm.enable();
    sm.update(status::value::FAULT_STATE);
    EXPECT_EQ(sm.controlWord(), control::word::FAULT_RESET);

    // First toggle (FAULT_RESET → 0)
    advanceClock(55ms);
    sm.update(status::value::FAULT_STATE);
    EXPECT_EQ(sm.controlWord(), 0);

    // Second toggle (0 → FAULT_RESET)
    advanceClock(55ms);
    sm.update(status::value::FAULT_STATE);
    EXPECT_EQ(sm.controlWord(), control::word::FAULT_RESET);
}

TEST_F(DS402StateMachineTest, enable_off_fault_cleared_stabilizes)
{
    sm.enable();
    sm.update(status::value::FAULT_STATE);

    // Fault clears: enter stabilization (controlword held at 0)
    sm.update(0);
    EXPECT_EQ(sm.controlWord(), 0);

    // Still stabilizing after a few ms
    sm.update(0);
    EXPECT_EQ(sm.controlWord(), 0);
}

TEST_F(DS402StateMachineTest, enable_off_fault_cleared_transitions_after_stabilization)
{
    sm.enable();
    sm.update(status::value::FAULT_STATE);

    // Fault clears
    sm.update(0);

    // Wait for stabilization delay
    advanceClock(110ms);
    sm.update(0);

    // Should be in SAFE_RESET now
    sm.update(0);
    EXPECT_EQ(sm.controlWord(), control::word::SHUTDOWN);
}

TEST_F(DS402StateMachineTest, enable_off_fault_returns_during_stabilization)
{
    sm.enable();
    sm.update(status::value::FAULT_STATE);

    // Fault clears, start stabilization
    sm.update(0);

    // Fault comes back during stabilization: toggle fires immediately
    sm.update(status::value::FAULT_STATE);
    EXPECT_TRUE(sm.isFaulted());
    EXPECT_EQ(sm.controlWord(), control::word::FAULT_RESET);

    // After 50ms, toggles to 0
    advanceClock(55ms);
    sm.update(status::value::FAULT_STATE);
    EXPECT_EQ(sm.controlWord(), 0);
}

// --- ENABLE: SAFE_RESET state ---

TEST_F(DS402StateMachineTest, safe_reset_sends_shutdown)
{
    reachSafeReset();

    sm.update(0);
    EXPECT_EQ(sm.controlWord(), control::word::SHUTDOWN);
}

TEST_F(DS402StateMachineTest, safe_reset_does_not_transition_before_delay)
{
    reachSafeReset();

    // Status word matches READY_TO_SWITCH_ON but delay hasn't elapsed.
    // If we were already past SAFE_RESET into PREPARE_TO_SWITCH_ON, this would
    // trigger a transition to SWITCH_ON -> ENABLE_OPERATION control word.
    sm.update(status::value::READY_TO_SWITCH_ON_STATE);
    sm.update(status::value::ON_STATE);

    EXPECT_EQ(sm.controlWord(), control::word::SHUTDOWN);
}

TEST_F(DS402StateMachineTest, safe_reset_transitions_after_delay)
{
    reachSafeReset();
    advanceClock(110ms);
    sm.update(0);

    // Now in PREPARE_TO_SWITCH_ON: READY_TO_SWITCH_ON triggers SWITCH_ON transition
    sm.update(status::value::READY_TO_SWITCH_ON_STATE);
    sm.update(status::value::ON_STATE);
    EXPECT_EQ(sm.controlWord(), control::word::ENABLE_OPERATION);
}

// --- ENABLE: PREPARE_TO_SWITCH_ON state ---

TEST_F(DS402StateMachineTest, prepare_to_switch_on_sends_shutdown)
{
    reachPrepareToSwitchOn();

    sm.update(0);
    EXPECT_EQ(sm.controlWord(), control::word::SHUTDOWN);
}

TEST_F(DS402StateMachineTest, prepare_to_switch_on_waits_for_ready_status)
{
    reachPrepareToSwitchOn();

    sm.update(0);
    sm.update(0);
    EXPECT_EQ(sm.controlWord(), control::word::SHUTDOWN);
}

TEST_F(DS402StateMachineTest, prepare_to_switch_on_transitions_on_ready_status)
{
    reachPrepareToSwitchOn();

    sm.update(status::value::READY_TO_SWITCH_ON_STATE);
    sm.update(status::value::ON_STATE);
    EXPECT_EQ(sm.controlWord(), control::word::ENABLE_OPERATION);
}

// --- ENABLE: SWITCH_ON state ---

TEST_F(DS402StateMachineTest, switch_on_sends_enable_operation)
{
    reachSwitchOn();

    sm.update(0);
    EXPECT_EQ(sm.controlWord(), control::word::ENABLE_OPERATION);
}

TEST_F(DS402StateMachineTest, switch_on_waits_for_on_status)
{
    reachSwitchOn();

    sm.update(0);
    sm.update(0);
    EXPECT_EQ(sm.controlWord(), control::word::ENABLE_OPERATION);
}

TEST_F(DS402StateMachineTest, switch_on_transitions_to_on)
{
    reachSwitchOn();

    sm.update(status::value::ON_STATE);
    EXPECT_EQ(sm.controlWord(), control::word::ENABLE_OPERATION);

    // Command should be reset: further updates via NONE don't change anything
    sm.update(0);
    EXPECT_EQ(sm.controlWord(), control::word::ENABLE_OPERATION);
}

// --- Full enable sequence ---

TEST_F(DS402StateMachineTest, full_enable_sequence_no_fault)
{
    sm.enable();

    // OFF (no fault) -> SAFE_RESET immediately
    sm.update(0);

    // In SAFE_RESET
    sm.update(0);
    EXPECT_EQ(sm.controlWord(), control::word::SHUTDOWN);

    // Wait for reset delay, then SAFE_RESET -> PREPARE_TO_SWITCH_ON
    advanceClock(110ms);
    sm.update(0);
    EXPECT_EQ(sm.controlWord(), control::word::SHUTDOWN);

    // PREPARE_TO_SWITCH_ON -> SWITCH_ON
    sm.update(status::value::READY_TO_SWITCH_ON_STATE);
    EXPECT_EQ(sm.controlWord(), control::word::SHUTDOWN);

    // SWITCH_ON -> ON
    sm.update(status::value::ON_STATE);
    EXPECT_EQ(sm.controlWord(), control::word::ENABLE_OPERATION);
}

TEST_F(DS402StateMachineTest, full_enable_sequence_with_fault)
{
    sm.enable();

    // OFF with fault: toggle fires immediately (FAULT_RESET)
    sm.update(status::value::FAULT_STATE);
    EXPECT_EQ(sm.controlWord(), control::word::FAULT_RESET);

    // Fault clears, enter stabilization
    sm.update(0);
    EXPECT_EQ(sm.controlWord(), 0);

    // Wait for stabilization
    advanceClock(110ms);
    sm.update(0);

    // Now in SAFE_RESET
    sm.update(0);
    EXPECT_EQ(sm.controlWord(), control::word::SHUTDOWN);

    // Complete the enable sequence
    advanceClock(110ms);
    sm.update(0);
    sm.update(status::value::READY_TO_SWITCH_ON_STATE);
    sm.update(status::value::ON_STATE);
    EXPECT_EQ(sm.controlWord(), control::word::ENABLE_OPERATION);
    EXPECT_TRUE(sm.isEnabled());
}

TEST_F(DS402StateMachineTest, enable_command_resets_after_reaching_on)
{
    reachOn();

    uint16_t cw = sm.controlWord();
    sm.update(0);
    EXPECT_EQ(sm.controlWord(), cw);

    sm.update(status::value::FAULT_STATE);
    EXPECT_EQ(sm.controlWord(), cw);
}

// --- Fault during enable sequence ---

TEST_F(DS402StateMachineTest, fault_during_safe_reset_returns_to_off)
{
    reachSafeReset();
    sm.update(0);
    EXPECT_EQ(sm.controlWord(), control::word::SHUTDOWN);

    // Fault during SAFE_RESET -> back to OFF
    sm.update(status::value::FAULT_STATE);

    // Now in OFF with fault, toggle starts
    advanceClock(55ms);
    sm.update(status::value::FAULT_STATE);
    EXPECT_EQ(sm.controlWord(), control::word::FAULT_RESET);
}

// --- Timeout ---

TEST_F(DS402StateMachineTest, timeout_in_prepare_to_switch_on_resets_to_off)
{
    reachPrepareToSwitchOn();

    advanceClock(2100ms);
    sm.update(0);

    // Timed out -> back to OFF (no fault), transitions to SAFE_RESET immediately
    // Next update enters SAFE_RESET
    sm.update(0);
    EXPECT_EQ(sm.controlWord(), control::word::SHUTDOWN);
}

TEST_F(DS402StateMachineTest, timeout_in_safe_reset_resets_to_off)
{
    reachSafeReset();

    advanceClock(2100ms);
    sm.update(0);

    // Timed out -> back to OFF, then immediately to SAFE_RESET
    sm.update(0);
    EXPECT_EQ(sm.controlWord(), control::word::SHUTDOWN);
}

TEST_F(DS402StateMachineTest, timeout_in_switch_on_resets_to_off)
{
    reachSwitchOn();

    advanceClock(2100ms);
    sm.update(0);

    // Timed out -> OFF
    sm.update(0);
    // OFF (no fault) -> SAFE_RESET, but control_word_ not yet updated
    sm.update(0);
    // Now in SAFE_RESET
    EXPECT_EQ(sm.controlWord(), control::word::SHUTDOWN);
}

// --- DISABLE ---

TEST_F(DS402StateMachineTest, disable_sends_disable_voltage)
{
    sm.disable();
    sm.update(0);

    EXPECT_EQ(sm.controlWord(), control::word::DISABLE_VOLTAGE);
}

TEST_F(DS402StateMachineTest, disable_transitions_to_off_on_off_status)
{
    sm.disable();
    sm.update(status::value::OFF_STATE);
    EXPECT_EQ(sm.controlWord(), control::word::DISABLE_VOLTAGE);

    // Command should be reset to NONE
    sm.update(0);
    EXPECT_EQ(sm.controlWord(), control::word::DISABLE_VOLTAGE);
}

TEST_F(DS402StateMachineTest, disable_from_on_state)
{
    reachOn();

    sm.disable();
    sm.update(0);
    EXPECT_EQ(sm.controlWord(), control::word::DISABLE_VOLTAGE);

    sm.update(status::value::OFF_STATE);
    EXPECT_EQ(sm.controlWord(), control::word::DISABLE_VOLTAGE);
}

TEST_F(DS402StateMachineTest, disable_during_enable_sequence)
{
    reachSafeReset();

    sm.disable();
    sm.update(0);
    EXPECT_EQ(sm.controlWord(), control::word::DISABLE_VOLTAGE);

    sm.update(status::value::OFF_STATE);
    EXPECT_EQ(sm.controlWord(), control::word::DISABLE_VOLTAGE);
}

// --- Re-enable ---

TEST_F(DS402StateMachineTest, enable_after_disable)
{
    reachOn();

    sm.disable();
    sm.update(status::value::OFF_STATE);

    sm.enable();
    sm.update(0);

    // Non-faulted -> SAFE_RESET, next update sends SHUTDOWN
    sm.update(0);
    EXPECT_EQ(sm.controlWord(), control::word::SHUTDOWN);
}

TEST_F(DS402StateMachineTest, enable_after_timeout)
{
    reachPrepareToSwitchOn();
    advanceClock(2100ms);
    sm.update(0);

    // Back in OFF with ENABLE command, no fault -> SAFE_RESET immediately
    sm.update(0);
    EXPECT_EQ(sm.controlWord(), control::word::SHUTDOWN);

    // Can complete the full sequence after a timeout
    advanceClock(110ms);
    sm.update(0);
    sm.update(status::value::READY_TO_SWITCH_ON_STATE);
    sm.update(status::value::ON_STATE);
    EXPECT_EQ(sm.controlWord(), control::word::ENABLE_OPERATION);
}

// --- isFaulted / State::FAULT ---

TEST_F(DS402StateMachineTest, is_faulted_reflects_status_word)
{
    EXPECT_FALSE(sm.isFaulted());

    sm.update(status::value::FAULT_STATE);
    EXPECT_TRUE(sm.isFaulted());

    sm.update(0);
    EXPECT_FALSE(sm.isFaulted());
}

TEST_F(DS402StateMachineTest, fault_while_on_disables_motor)
{
    reachOn();
    EXPECT_TRUE(sm.isEnabled());

    // Fault while idle (command = NONE)
    sm.update(status::value::FAULT_STATE);
    EXPECT_TRUE(sm.isFaulted());
    EXPECT_FALSE(sm.isEnabled());
}

TEST_F(DS402StateMachineTest, enable_from_fault_state)
{
    // Drive faults while idle
    sm.update(status::value::FAULT_STATE);
    EXPECT_TRUE(sm.isFaulted());

    // User calls enable: FAULT -> OFF -> starts fault reset toggle
    sm.enable();
    sm.update(status::value::FAULT_STATE);

    advanceClock(55ms);
    sm.update(status::value::FAULT_STATE);
    EXPECT_EQ(sm.controlWord(), control::word::FAULT_RESET);
}
