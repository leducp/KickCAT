#ifndef KICKCAT_COE_CiA_DS402_STATE_MACHINE_H
#define KICKCAT_COE_CiA_DS402_STATE_MACHINE_H

#include "kickcat/OS/Time.h"

namespace kickcat::CoE::CiA::DS402
{
    // status stores the possible CAN states
    // For more infos, see DS402, page 35
    // CANopen DS402  0x6041 Statusword
    namespace status
    {
        namespace masks
        {
            // Status bits
            constexpr uint16_t STATUS_MASK           = 0x7F;
            constexpr uint16_t READY_TO_SWITCH_ON    = 1U << 0;
            constexpr uint16_t SWITCHED_ON           = 1U << 1;
            constexpr uint16_t OPERATION_ENABLE      = 1U << 2;
            constexpr uint16_t FAULT_MODE            = 1U << 3;
            constexpr uint16_t VOLTAGE_ENABLED       = 1U << 4;
            constexpr uint16_t QUICK_STOP            = 1U << 5;
            constexpr uint16_t SWITCH_ON_DISABLED    = 1U << 6;
            constexpr uint16_t WARNING               = 1U << 7;
            constexpr uint16_t REMOTE                = 1U << 9;
            constexpr uint16_t TARGET_REACHED        = 1U << 10;
            constexpr uint16_t INTERNAL_LIMIT_ACTIVE = 1U << 11;
            constexpr uint16_t SETPOINT_ACKNOWLEDGE  = 1U << 12;
        }

        namespace value
        {
            /// \brief Some status word values corresponding to CANOpen states.
            constexpr uint16_t OFF_STATE =  masks::SWITCH_ON_DISABLED;
            constexpr uint16_t READY_TO_SWITCH_ON_STATE = masks::VOLTAGE_ENABLED | masks::READY_TO_SWITCH_ON;
            constexpr uint16_t ON_STATE = masks::QUICK_STOP | masks::VOLTAGE_ENABLED | masks::OPERATION_ENABLE | masks::SWITCHED_ON | masks::READY_TO_SWITCH_ON;
            constexpr uint16_t DISABLED_STATE = masks::QUICK_STOP | masks::VOLTAGE_ENABLED | masks::SWITCHED_ON | masks::READY_TO_SWITCH_ON;
            constexpr uint16_t FAULT_STATE = masks::FAULT_MODE;
        }
    }


    // control stores the possible control word values
    // For more infos, see DS402, page 35
    // CANopen DS402  0x6040 Controlword
    namespace control
    {
        namespace word
        {
            constexpr uint16_t SHUTDOWN                       = 0x0006U;
            constexpr uint16_t SWITCH_ON_OR_DISABLE_OPERATION = 0x0007U;
            constexpr uint16_t ENABLE_OPERATION               = 0x000FU;
            constexpr uint16_t FAULT_RESET                    = 0x0080U;
            constexpr uint16_t DISABLE_VOLTAGE                = 0x0000U;
            constexpr uint16_t QUICK_STOP                     = 0x0002U;
            constexpr uint16_t SET_ABS_POINT_NOBLEND          = 0x001FU;
            constexpr uint16_t SET_POINT_RESET                = 0x000FU;
        }

        // controlmode stores the available CAN control mode (also called "Mode of Operation")
        // For more infos, see DS402, page 37
        enum ControlMode
        {
            NO_MODE    = -1, // Default value to mean "error occurred"
            POSITION   = 1,  // Profiled position (point to point) mode
            VELOCITY   = 3,  // Profiled velocity mode
            TORQUE     = 4,   // Profiled torque mode
            POSITION_CYCLIC = 8 // Direct position control without ramps
        };

    }

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
