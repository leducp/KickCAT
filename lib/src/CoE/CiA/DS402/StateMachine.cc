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
                        if ((status_word_ & status::value::FAULT_STATE) == status::value::FAULT_STATE)
                        {
                            was_faulted_ = true;
                            stabilizing_ = false;

                            // Toggle bit 7 at ~10Hz to generate the rising edge required
                            // by DS402 to acknowledge a fault. A faster rate can cause
                            // some drives to stop responding.
                            if (elapsed_time(toggle_timestamp_) >= FAULT_RESET_HALF_PERIOD)
                            {
                                fault_reset_active_ = not fault_reset_active_;
                                toggle_timestamp_ = since_epoch();
                            }

                            if (fault_reset_active_)
                            {
                                control_word_ = control::word::FAULT_RESET;
                            }
                            else
                            {
                                control_word_ = 0;
                            }
                        }
                        else if (was_faulted_)
                        {
                            // Fault has cleared. Hold controlword at 0 (neutral) for a
                            // stabilization period before sending SHUTDOWN, to prevent
                            // drives from re-faulting immediately.
                            control_word_ = 0;
                            if (not stabilizing_)
                            {
                                stabilizing_ = true;
                                stabilization_timestamp_ = since_epoch();
                            }
                            else if (elapsed_time(stabilization_timestamp_) >= STABILIZATION_DELAY)
                            {
                                motor_state_ = State::SAFE_RESET;
                                was_faulted_ = false;
                                stabilizing_ = false;
                            }
                        }
                        else
                        {
                            // No fault, proceed directly
                            motor_state_ = State::SAFE_RESET;
                        }
                        break;
                    }
                    case State::SAFE_RESET:
                    {
                        control_word_ = control::word::SHUTDOWN;
                        if (elapsed_time(start_motor_timestamp_) > MOTOR_RESET_DELAY)
                        {
                            motor_state_ = State::PREPARE_TO_SWITCH_ON;
                        }
                        break;
                    }
                    case State::PREPARE_TO_SWITCH_ON:
                    {
                        control_word_ = control::word::SHUTDOWN;
                        if ((status_word_ & status::value::READY_TO_SWITCH_ON_STATE) == status::value::READY_TO_SWITCH_ON_STATE)
                        {
                            motor_state_ = State::SWITCH_ON;
                        }
                        break;
                    }
                    case State::SWITCH_ON:
                    {
                        control_word_ = control::word::ENABLE_OPERATION;
                        if ((status_word_ & status::value::ON_STATE) == status::value::ON_STATE)
                        {
                            motor_state_ = State::ON;

                            // Reset command now that the desired state has been reached.
                            command_ = Command::NONE;
                        }
                        break;
                    }
                    case State::ON:
                    {
                        coe_info("ON status achieved\n");
                        break;
                    }
                    case State::FAULT:
                    {
                        motor_state_ = State::OFF;
                        break;
                    }
                    default:
                    {
                        break;
                    }
                }

                // Fault detected during enable sequence: restart immediately
                if ( (motor_state_ != State::OFF)
                    and (motor_state_ != State::FAULT)
                    and ((status_word_ & status::value::FAULT_STATE) == status::value::FAULT_STATE)
                    )
                {
                    coe_error("Motor faulted during enable sequence, returning to OFF state.\n");
                    motor_state_ = State::OFF;
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
                control_word_ = control::word::DISABLE_VOLTAGE;
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
                if ((status_word_ & status::value::FAULT_STATE) == status::value::FAULT_STATE)
                {
                    motor_state_ = State::FAULT;
                }
                break;
            }
        }
    }
}
