#include "CanOpenStateMachine.h"
#include "kickcat/Time.h"
#include "kickcat/Error.h"

namespace can
{
    void CANOpenStateMachine::update(uint16_t statusWord)
        {
            statusWord_ = statusWord;

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
                            motorState_ = CANOpenState::SAFE_RESET;
                            break;
                        }
                        case CANOpenState::SAFE_RESET:
                        {
                            controlWord_ = control::word::SHUTDOWN;
                            if (kickcat::elapsed_time(startMotorTimestamp_) > MOTOR_RESET_DELAY)
                            {
                                motorState_ = CANOpenState::PREPARE_TO_SWITCH_ON ;
                            }
                            break;
                        }
                        case CANOpenState::PREPARE_TO_SWITCH_ON:
                        {
                            controlWord_ = control::word::SHUTDOWN;
                            if ((statusWord_ & status::value::READY_TO_SWITCH_ON_STATE) == status::value::READY_TO_SWITCH_ON_STATE)
                            {
                                motorState_ = CANOpenState::SWITCH_ON;
                            }
                            break;
                        }
                        case CANOpenState::SWITCH_ON:
                        {
                            controlWord_ = control::word::ENABLE_OPERATION;
                            if ((statusWord_ & status::value::ON_STATE)== status::value::ON_STATE)
                            {
                                motorState_ = CANOpenState::ON ;

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
                    if (motorState_ != CANOpenState::ON
                        && motorState_ != CANOpenState::FAULT
                        && motorState_ != CANOpenState::OFF
                        && kickcat::elapsed_time(startMotorTimestamp_) > MOTOR_INIT_TIMEOUT
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
}
