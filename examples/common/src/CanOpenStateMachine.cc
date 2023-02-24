#include "CanOpenStateMachine.h"
#include "kickcat/Time.h"
#include "kickcat/Error.h"

namespace kickcat
{
    void CANOpenStateMachine::update(uint16_t status_word)
    {
        status_word_ = status_word;

        switch (command_)
        {
            case CANOpenCommand::ENABLE:
            {
                switch (motor_state_)
                {
                    case CANOpenState::OFF:
                    {
                        start_motor_timestamp_ = since_epoch();
                        control_word_ = control::word::FAULT_RESET;
                        motor_state_ = CANOpenState::SAFE_RESET;
                        break;
                    }
                    case CANOpenState::SAFE_RESET:
                    {
                        control_word_ = control::word::SHUTDOWN;
                        if (elapsed_time(start_motor_timestamp_) > MOTOR_RESET_DELAY)
                        {
                            motor_state_ = CANOpenState::PREPARE_TO_SWITCH_ON ;
                        }
                        break;
                    }
                    case CANOpenState::PREPARE_TO_SWITCH_ON:
                    {
                        control_word_ = control::word::SHUTDOWN;
                        if ((status_word_ & status::value::READY_TO_SWITCH_ON_STATE) == status::value::READY_TO_SWITCH_ON_STATE)
                        {
                            motor_state_ = CANOpenState::SWITCH_ON;
                        }
                        break;
                    }
                    case CANOpenState::SWITCH_ON:
                    {
                        control_word_ = control::word::ENABLE_OPERATION;
                        if ((status_word_ & status::value::ON_STATE)== status::value::ON_STATE)
                        {
                            motor_state_ = CANOpenState::ON ;

                            // Reset command now that the desired state has been reached.
                            command_ = CANOpenCommand::NONE;
                        }
                        break;
                    }
                    default:
                    case CANOpenState::ON: DEBUG_PRINT("ON status achieved\n"); break;
                    case CANOpenState::FAULT:
                        // Do nothing
                        break;
                }

                // Time out, motor start failed
                if ( (motor_state_ != CANOpenState::ON)
                    and (motor_state_ != CANOpenState::FAULT)
                    and (motor_state_ != CANOpenState::OFF)
                    and (elapsed_time(start_motor_timestamp_) > MOTOR_INIT_TIMEOUT)
                    )
                {
                    DEBUG_PRINT("Can't enable motor: timeout, start again from OFF state.\n");
                    motor_state_ = CANOpenState::OFF;
                }
                break;
            }
            case CANOpenCommand::DISABLE:
            {
                control_word_ = control::word::DISABLE_VOLTAGE;
                if ((status::value::OFF_STATE & status_word_) == status::value::OFF_STATE)
                {
                    motor_state_ = CANOpenState::OFF;

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
}
