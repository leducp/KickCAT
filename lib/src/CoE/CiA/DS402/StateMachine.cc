#include "kickcat/OS/Time.h"
#include "kickcat/debug.h"
#include "CoE/CiA/DS402/StateMachine.h"

namespace kickcat::CoE::CiA::DS402
{
    void StateMachine::update(uint16_t status_word)
    {
        status_word_ = status_word;

        switch (command_)
        {
            case Command::ENABLE:
            {
                switch (motor_state_)
                {
                    case State::OFF:
                    {
                        start_motor_timestamp_ = since_epoch();
                        control_word_ = control::word::FAULT_RESET | control::word::DISABLE_BRAKE;
                        if ((status_word_ & status::value::FAULT_STATE) != status::value::FAULT_STATE)
                        {
                            motor_state_ = State::SAFE_RESET;
                        }
                        break;
                    }
                    case State::SAFE_RESET:
                    {
                        control_word_ = control::word::SHUTDOWN | control::word::DISABLE_BRAKE;
                        if (elapsed_time(start_motor_timestamp_) > MOTOR_RESET_DELAY)
                        {
                            motor_state_ = State::PREPARE_TO_SWITCH_ON ;
                        }
                        break;
                    }
                    case State::PREPARE_TO_SWITCH_ON:
                    {
                        control_word_ = control::word::SHUTDOWN | control::word::DISABLE_BRAKE;
                        if ((status_word_ & status::value::READY_TO_SWITCH_ON_STATE) == status::value::READY_TO_SWITCH_ON_STATE)
                        {
                            motor_state_ = State::SWITCH_ON;
                        }
                        break;
                    }
                    case State::SWITCH_ON:
                    {
                        control_word_ = control::word::ENABLE_OPERATION | control::word::DISABLE_BRAKE;
                        if ((status_word_ & status::value::ON_STATE)== status::value::ON_STATE)
                        {
                            motor_state_ = State::ON ;

                            // Reset command now that the desired state has been reached.
                            command_ = Command::NONE;
                        }
                        break;
                    }
                    default:
                    case State::ON:
                    {
                        coe_info("ON status achieved\n");
                        break;
                    }
                    case State::FAULT:
                    {
                        // Do nothing
                        break;
                    }
                }

                // Time out, motor start failed
                if ( (motor_state_ != State::ON)
                    and (motor_state_ != State::FAULT)
                    and (motor_state_ != State::OFF)
                    and (elapsed_time(start_motor_timestamp_) > MOTOR_INIT_TIMEOUT)
                    )
                {
                    coe_error("Can't enable motor: timeout, start again from OFF state.\n");
                    motor_state_ = State::OFF;
                }
                break;
            }
            case Command::DISABLE:
            {
                control_word_ = control::word::DISABLE_VOLTAGE | control::word::DISABLE_BRAKE;
                if ((status::value::OFF_STATE & status_word_) == status::value::OFF_STATE)
                {
                    motor_state_ = State::OFF;

                    // Reset command now that the desired state has been reached.
                    command_ = Command::NONE;
                }
                break;
            }
            case Command::NONE:
            default:
            {
                break;
            }
        }
    }
}
