#ifndef KICKCAT_CAN_ELMO_STATE_MACHINE_H
#define KICKCAT_CAN_ELMO_STATE_MACHINE_H

#include "kickcat/Time.h"

namespace kickcat
{
    /// \enum CANOpenState The different states of a DS402-compliant device.
    enum class CANOpenState
    {
        OFF,
        SAFE_RESET,
        PREPARE_TO_SWITCH_ON,
        SWITCH_ON,
        ON,
        FAULT
    };

    /// \enum CANOpenrCommand The command to send to the CANOpen state machine.
    enum class CANOpenCommand
    {
        NONE,
        ENABLE,
        DISABLE,
    };

    // status stores the possible CAN states
    // For more infos, see DS402, page 35
    // CANopen DS402  0x6041 Statusword
    namespace status
    {
        namespace masks
        {
            // Status bits
            uint16_t const STATUS_MASK           = 0x7F;
            uint16_t const READY_TO_SWITCH_ON    = 1U << 0;
            uint16_t const SWITCHED_ON           = 1U << 1;
            uint16_t const OPERATION_ENABLE      = 1U << 2;
            uint16_t const FAULT_MODE            = 1U << 3;
            uint16_t const VOLTAGE_ENABLED       = 1U << 4;
            uint16_t const QUICK_STOP            = 1U << 5;
            uint16_t const SWITCH_ON_DISABLED    = 1U << 6;
            uint16_t const WARNING               = 1U << 7;
            uint16_t const REMOTE                = 1U << 9;
            uint16_t const TARGET_REACHED        = 1U << 10;
            uint16_t const INTERNAL_LIMIT_ACTIVE = 1U << 11;
            uint16_t const SETPOINT_ACKNOWLEDGE  = 1U << 12;
        }

        namespace value
        {
            /// \brief Some status word values corresponding to CANOpen states.
            uint16_t const OFF_STATE =  masks::SWITCH_ON_DISABLED;
            uint16_t const READY_TO_SWITCH_ON_STATE = masks::VOLTAGE_ENABLED | masks::READY_TO_SWITCH_ON;
            uint16_t const ON_STATE = masks::QUICK_STOP | masks::VOLTAGE_ENABLED | masks::OPERATION_ENABLE | masks::SWITCHED_ON | masks::READY_TO_SWITCH_ON;
            uint16_t const DISABLED_STATE = masks::QUICK_STOP | masks::VOLTAGE_ENABLED | masks::SWITCHED_ON | masks::READY_TO_SWITCH_ON;
            uint16_t const FAULT_STATE = masks::FAULT_MODE;
        }
    }


    // control stores the possible control word values
    // For more infos, see DS402, page 35
    // CANopen DS402  0x6040 Controlword
    namespace control
    {
        namespace word
        {
            uint16_t const SHUTDOWN                       = 0x0006U;
            uint16_t const SWITCH_ON_OR_DISABLE_OPERATION = 0x0007U;
            uint16_t const ENABLE_OPERATION               = 0x000FU;
            uint16_t const FAULT_RESET                    = 0x0080U;
            uint16_t const DISABLE_VOLTAGE                = 0x0000U;
            uint16_t const QUICK_STOP                     = 0x0002U;
            uint16_t const SET_ABS_POINT_NOBLEND          = 0x001FU;
            uint16_t const SET_POINT_RESET                = 0x000FU;
        }

        // controlmode stores the available CAN control mode (also called "Mode of Operation")
        // For more infos, see DS402, page 37
        enum CANOpenControlMode
        {
            NO_MODE    = -1, // Default value to mean "error occurred"
            POSITION   = 1,  // Profiled position (point to point) mode
            VELOCITY   = 3,  // Profiled velocity mode
            TORQUE     = 4,   // Profiled torque mode
            POSITION_CYCLIC = 8 // Direct position control without ramps
        };

    }

    // Timeouts to prevent blocking the statemachine in case of failure
    constexpr nanoseconds MOTOR_RESET_DELAY = 10ms;
    constexpr nanoseconds MOTOR_INIT_TIMEOUT = 1s;

    class CANOpenStateMachine
    {
    public:
        void update(uint16_t status_word);
        void setCommand(CANOpenCommand command) { command_ = command; };
        uint16_t getControlWord() { return control_word_; }

    private:
        CANOpenCommand command_  = CANOpenCommand::NONE;
        CANOpenState motor_state_ = CANOpenState::OFF;
        nanoseconds start_motor_timestamp_ = 0ns;
        uint16_t control_word_ = 0;
        uint16_t status_word_ = 0;
    };
}

#endif
