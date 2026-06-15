#ifndef KICKCAT_COE_CiA_DS402_PROTOCOL_H
#define KICKCAT_COE_CiA_DS402_PROTOCOL_H

#include <cstdint>

// CiA-402 (DS402) protocol constants: statusword (0x6041) / controlword (0x6040)
// bit definitions and the modes of operation. No behaviour -- just the wire
// definitions, so both the master StateMachine and slave-side emulations can
// share them. See DS402, pages 35 / 37.
namespace kickcat::CoE::CiA::DS402
{
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

        // Mode of Operation (0x6060/0x6061). See DS402, page 37.
        enum ControlMode
        {
            NO_MODE         = -1, // Default value to mean "error occurred"
            POSITION        = 1,  // Profiled position (point to point) mode
            VELOCITY        = 3,  // Profiled velocity mode
            TORQUE          = 4,  // Profiled torque mode
            POSITION_CYCLIC = 8,  // Cyclic synchronous position (CSP)
            VELOCITY_CYCLIC = 9,  // Cyclic synchronous velocity (CSV)
            TORQUE_CYCLIC   = 10  // Cyclic synchronous torque   (CST)
        };
    }
}

#endif
