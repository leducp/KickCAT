#ifndef KICKCAT_COE_CiA_DS402_STATE_MACHINE_H
#define KICKCAT_COE_CiA_DS402_STATE_MACHINE_H

#include "kickcat/OS/Time.h"
#include "kickcat/CoE/CiA/DS402/protocol.h"

namespace kickcat::CoE::CiA::DS402
{
    // Timeouts to prevent blocking the statemachine in case of failure
    constexpr nanoseconds MOTOR_RESET_DELAY = 100ms;
    constexpr nanoseconds MOTOR_INIT_TIMEOUT = 2s;

    // Fault reset toggle: DS402 requires a rising edge on controlword bit 7 to
    // acknowledge a fault. We toggle bit 7 with this half-period (~10Hz).
    constexpr nanoseconds FAULT_RESET_HALF_PERIOD = 50ms;

    // After a fault clears, hold controlword at 0 (neutral) for this duration
    // before sending SHUTDOWN, to let the drive settle.
    constexpr nanoseconds STABILIZATION_DELAY = 100ms;

    class StateMachine
    {
    public:
        // Update the state machine internal state and compute control word
        void update(uint16_t status_word);

        // Control word shall be send to the motor at every loop
        uint16_t controlWord() const { return control_word_; }

        // General control over the state machine
        void enable()  { command_ = Command::ENABLE;  }
        void disable() { command_ = Command::DISABLE; }

        bool isEnabled() const { return motor_state_ == State::ON; }
        bool isFaulted() const { return (status_word_ & status::value::FAULT_STATE) == status::value::FAULT_STATE; }

    private:
        enum class Command
        {
            NONE,
            ENABLE,
            DISABLE,
        };
        Command command_  = Command::NONE;

        enum class State
        {
            OFF,
            SAFE_RESET,
            PREPARE_TO_SWITCH_ON,
            SWITCH_ON,
            ON,
            FAULT
        };
        State motor_state_ = State::OFF;

        nanoseconds start_motor_timestamp_ = 0ns;
        uint16_t control_word_ = 0;
        uint16_t status_word_ = 0;

        nanoseconds toggle_timestamp_ = 0ns;
        bool fault_reset_active_ = false;

        bool was_faulted_ = false;
        bool stabilizing_ = false;
        nanoseconds stabilization_timestamp_ = 0ns;
    };
}

#endif
