///////////////////////////////////////////////////////////////////////////////
/// \copyright Wandercraft
///////////////////////////////////////////////////////////////////////////////
#ifndef CAN_ELMO_STATE_MACHINE_H
#define CAN_ELMO_STATE_MACHINE_H


#include "kickcat/Bus.h"
#include "kickcat/Time.h"


namespace can
{
    /// \enum CANOpenState The different states of a CANOpen motor.
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
    // For more infos, see "Docs/CANopen DS402.pdf", page 35
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
    // For more infos, see "Docs/CANopen DS402.pdf", page 35
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
        // For more infos, see "Docs/CANopen DS402.pdf", page 37
        enum CANOpenControlMode
        {
            NO_MODE    = -1, // Default value to mean "error occurred"
            POSITION   = 1,  // Profiled position (point to point) mode
            VELOCITY   = 3,  // Profiled velocity mode
            TORQUE     = 4,   // Profiled torque mode
            POSITION_CYCLIC = 8 // Direct position control without ramps
        };

    }

    using namespace std::chrono;
    class CANOpenStateMachine
    {
    public:
        CANOpenCommand command_  = CANOpenCommand::NONE;
        CANOpenState motorState_ = CANOpenState::OFF;
        nanoseconds startMotorTimestamp_ = 0ns;
        uint16_t controlWord_ = 0;
        uint16_t statusWord_ = 0;

        void update()
        {
            switch (command_)
            {
                case CANOpenCommand::ENABLE:
                {
                    switch (motorState_)
                    {
                        case CANOpenState::OFF:
                        {
                            startMotorTimestamp_ = kickcat::since_epoch();
                            controlWord_ = control::word::FAULT_RESET;
                            //printf("CW : FAULT RESET\n");
                            motorState_ = CANOpenState::SAFE_RESET;
                        }
                        break;

                        case CANOpenState::SAFE_RESET:
                            controlWord_ = control::word::SHUTDOWN;
                            //printf("CW : SHUTDOWN\n");
                            if (kickcat::elapsed_time(startMotorTimestamp_) > 10ms)
                            {
                                motorState_ = CANOpenState::PREPARE_TO_SWITCH_ON ;
                            }
                            break;
                        case CANOpenState::PREPARE_TO_SWITCH_ON:
                            controlWord_ = control::word::SHUTDOWN;
                            //printf("CW : SHUTDOWN 2\n");
                            if ((statusWord_ & status::value::READY_TO_SWITCH_ON_STATE) == status::value::READY_TO_SWITCH_ON_STATE)
                            {
                                motorState_ = CANOpenState::SWITCH_ON;
                            }
                            break;
                        case CANOpenState::SWITCH_ON:
                            controlWord_ = control::word::ENABLE_OPERATION;
                            //printf("CW : ENABLE OPERATION\n");
                            if ((statusWord_ & status::value::ON_STATE)== status::value::ON_STATE)
                            {
                                motorState_ = CANOpenState::ON ;

                                // Reset command now that the desired state has been reached.
                                command_ = CANOpenCommand::NONE;
                            }
                            break;
                        default:
                        case CANOpenState::ON: printf("ON status achieved\n"); break;
                        case CANOpenState::FAULT:
                            // Do nothing
                            break;
                    }

                    // Time out, motor start failed
                    if (motorState_ != CANOpenState::ON
                        && motorState_ != CANOpenState::FAULT
                        && motorState_ != CANOpenState::OFF
                        && kickcat::elapsed_time(startMotorTimestamp_) > 1.0s
                        )
                    {
                        DEBUG_PRINT("Can't enable motor: timeout, start again from OFF state.\n");
                        motorState_ = CANOpenState::OFF;
                    }
                    break;
                }
                case CANOpenCommand::DISABLE:
                {
                    controlWord_ = control::word::DISABLE_VOLTAGE;
                    if ((status::value::OFF_STATE & statusWord_) == status::value::OFF_STATE)
                    {
                        motorState_ = CANOpenState::OFF;

                        // Reset command now that the desired state has been reached.
                        command_ = CANOpenCommand::NONE;
                    }
                    break;
                }
                case CANOpenCommand::NONE:
                default:
                {
                    break;
                }
            }
        }

        void setCommand(CANOpenCommand command)
        {
            command_ = command;
        }

        void printState()
        {
            if ((statusWord_ & can::status::value::OFF_STATE) == can::status::value::OFF_STATE) {printf("State is OFF\n");}
            else if ((statusWord_ & can::status::value::ON_STATE) == can::status::value::ON_STATE) {printf("State is ON \n");}
            else if ((statusWord_ & can::status::value::DISABLED_STATE) == can::status::value::DISABLED_STATE) {printf("State is DISABLED \n");}
            else if ((statusWord_ & can::status::value::READY_TO_SWITCH_ON_STATE) == can::status::value::READY_TO_SWITCH_ON_STATE) {printf("State is READY_TO_SWITCH_ON \n");}
            else if ((statusWord_ & can::status::value::FAULT_STATE) == can::status::value::FAULT_STATE) {printf("State is FAULT\n");}
        }

        bool isON()
        {
            return((statusWord_ & can::status::value::ON_STATE) == can::status::value::ON_STATE);
        }

        bool isOFF()
        {
            return((statusWord_ & can::status::value::OFF_STATE) == can::status::value::OFF_STATE);
        }
    };
}

#endif // CAN_ELMO_STATE_MACHINE_H
