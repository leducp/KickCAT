#include <inttypes.h>

#include "kickcat/AbstractESC.h"
#include "kickcat/debug.h"
#include "protocol.h"


namespace kickcat
{
    void reportError(hresult const& rc)
    {
        if (rc != hresult::OK)
        {
            printf("\nERROR: %s code %" PRIu32 "\n", toString(rc), static_cast<uint32_t>(rc));
        }
    }


    bool AbstractESC::is_valid_sm(SyncManagerConfig const& sm_ref)
    {
        auto create_sm_address = [](uint16_t reg, uint16_t sm_index) { return reg + sm_index * 8; };

        SyncManager sm_read;

        read(create_sm_address(reg::SYNC_MANAGER, sm_ref.index), &sm_read, sizeof(sm_read));

        bool is_valid = (sm_read.start_address == sm_ref.start_address) and (sm_read.length == sm_ref.length)
                        and ((sm_read.control & SYNC_MANAGER_CONTROL_OPERATION_MODE_MASK)
                             == (sm_ref.control & SYNC_MANAGER_CONTROL_OPERATION_MODE_MASK))
                        and ((sm_read.control & SYNC_MANAGER_CONTROL_DIRECTION_MASK)
                             == (sm_ref.control & SYNC_MANAGER_CONTROL_DIRECTION_MASK))
                        and sm_read.activate == 1;

        // printf("SM read %i: start address %x, length %u, control %x, status %x, activate %x \n", sm_ref.index, sm_read.start_address, sm_read.length, sm_read.control, sm_read.status, sm_read.activate);
        // printf("SM config %i: start address %x, length %u, control %x \n", sm_ref.index, sm_ref.start_address, sm_ref.length, sm_ref.control);
        return is_valid;
    }

    bool AbstractESC::are_valid_sm(std::vector<SyncManagerConfig> const& sync_managers)
    {
        bool valid = true;
        for (auto& sm : sync_managers)
        {
            valid &= is_valid_sm(sm);
        }
        return valid;
    }


    void AbstractESC::set_sm_activate(SyncManagerConfig const& sm_conf, bool is_activated)
    {
        auto create_sm_address = [](uint16_t reg, uint16_t sm_index) { return reg + sm_index * 8; };

        SyncManager sm;
        read(create_sm_address(reg::SYNC_MANAGER, sm_conf.index), &sm, sizeof(sm));

        uint8_t sm_deactivated = 0x1;
        if (is_activated)
        {
            sm.pdi_control &= ~sm_deactivated;
        }
        else
        {
            sm.pdi_control |= sm_deactivated;
        }
        write(create_sm_address(reg::SYNC_MANAGER, sm_conf.index), &sm, sizeof(sm));
    }

    void AbstractESC::set_mailbox_config(std::vector<SyncManagerConfig> const& mailbox)
    {
        sm_mailbox_configs_ = mailbox;
    }


    void AbstractESC::set_process_data_input(uint8_t* buffer, SyncManagerConfig const& config)
    {
        sm_process_data_configs_.push_back(config);
        process_data_input_ = buffer;
        sm_pd_input_        = config;
    }


    void AbstractESC::set_process_data_output(uint8_t* buffer, SyncManagerConfig const& config)
    {
        sm_process_data_configs_.push_back(config);
        process_data_output_ = buffer;
        sm_pd_output_        = config;
    }


    void AbstractESC::routine()
    {
        read(reg::AL_CONTROL, &al_control_, sizeof(al_control_));
        read(reg::AL_STATUS, &al_status_, sizeof(al_status_));
        read(reg::WDOG_STATUS, &watchdog_, sizeof(watchdog_));

        if ((al_control_ & State::MASK_STATE) == State::INIT)
        {
            set_al_status(State::INIT);
            clear_error();
            for (auto& sm : sm_process_data_configs_)
            {
                set_sm_activate(sm, false);
            }
            for (auto& sm : sm_mailbox_configs_)
            {
                set_sm_activate(sm, false);
            }
        }

        if ((al_control_ & State::ERROR_ACK))
        {
            clear_error();
        }

        // Do nothing until error is acknowledged by master.
        if (al_status_ & State::ERROR_ACK)
        {
            return;
        }

        // BOOTSTRAP not supported yet. If implemented, need to check the SII to know if enabled.
        if ((al_control_ & State::MASK_STATE) == State::BOOT)
        {
            if ((al_status_ & State::MASK_STATE) == State::INIT)
            {
                set_error(StatusCode::BOOTSTRAP_NOT_SUPPORTED);
            }
            else
            {
                set_error(StatusCode::INVALID_REQUESTED_STATE_CHANGE);
            }
        }

        // Handle unsupported state
        uint8_t asked_state = al_control_ & State::MASK_STATE;
        if (asked_state != State::BOOT and asked_state != State::INIT and asked_state != State::PRE_OP
            and asked_state != State::SAFE_OP and asked_state != State::OPERATIONAL)
        {
            set_error(StatusCode::UNKNOWN_REQUESTED_STATE);
        }

        // ETG 1000.6 Table 99 – Primitives issued by ESM to Application
        switch (al_status_ & State::MASK_STATE)
        {
            case State::INIT:
            {
                routine_init();
                break;
            }

            case State::PRE_OP:
            {
                routine_preop();
                break;
            }

            case State::SAFE_OP:
            {
                routine_safeop();
                break;
            }

            case State::OPERATIONAL:
            {
                routine_op();
                break;
            }
            default:
            {
                printf("Unknown or error al_status %x \n", al_status_);
            }
        }

        write(reg::AL_STATUS_CODE, &al_status_code_, sizeof(al_status_code_));
        write(reg::AL_STATUS, &al_status_, sizeof(al_status_));
    }


    void AbstractESC::routine_init()
    {
        // TODO AL_CONTROL device identification flash led 0x0138 RUN LED Override
        if ((al_control_ & State::MASK_STATE) == State::PRE_OP)
        {
            if (are_valid_sm(sm_mailbox_configs_))
            {
                for (auto& sm : sm_mailbox_configs_)
                {
                    set_sm_activate(sm, true);
                }

                set_al_status(State::PRE_OP);
            }
            else
            {
                set_error(StatusCode::INVALID_MAILBOX_CONFIGURATION_PREOP);
            }
        }

        if (((al_control_ & State::MASK_STATE) == State::SAFE_OP)
            or ((al_control_ & State::MASK_STATE) == State::OPERATIONAL))
        {
            set_error(StatusCode::INVALID_REQUESTED_STATE_CHANGE);
        }
    }


    void AbstractESC::routine_preop()
    {
        if (not are_valid_sm(sm_mailbox_configs_))
        {
            for (auto& sm : sm_mailbox_configs_)
            {
                set_sm_activate(sm, false);
            }

            set_al_status(State::INIT);
            set_error(StatusCode::INVALID_MAILBOX_CONFIGURATION_PREOP);
        }

        if ((al_control_ & State::MASK_STATE) == State::SAFE_OP and not(al_status_ & State::ERROR_ACK))
        {
            // check process data SM
            for (auto& sm : sm_process_data_configs_)
            {
                if (not is_valid_sm(sm))
                {
                    if (sm.type == SyncManagerType::Input)
                    {
                        set_error(StatusCode::INVALID_INPUT_CONFIGURATION);
                    }
                    else if (sm.type == SyncManagerType::Output)
                    {
                        set_error(StatusCode::INVALID_OUTPUT_CONFIGURATION);
                    }
                    return;
                }
                set_sm_activate(sm, true);
            }

            set_al_status(State::SAFE_OP);
        }

        if ((al_control_ & State::MASK_STATE) == State::OPERATIONAL)
        {
            set_error(StatusCode::INVALID_REQUESTED_STATE_CHANGE);
        }
    }


    void AbstractESC::routine_safeop()
    {
        update_process_data_input();
        update_process_data_output();

        if (not are_valid_sm(sm_mailbox_configs_))
        {
            for (auto& sm : sm_mailbox_configs_)
            {
                set_sm_activate(sm, false);
            }

            for (auto& sm : sm_process_data_configs_)
            {
                set_sm_activate(sm, false);
            }

            set_al_status(State::INIT);
            set_error(StatusCode::INVALID_MAILBOX_CONFIGURATION_PREOP);
        }

        bool is_sm_process_data_invalid = false;
        for (auto& sm : sm_process_data_configs_)
        {
            if (not is_valid_sm(sm))
            {
                if (sm.type == SyncManagerType::Input)
                {
                    set_error(StatusCode::INVALID_INPUT_CONFIGURATION);
                }
                else if (sm.type == SyncManagerType::Output)
                {
                    set_error(StatusCode::INVALID_OUTPUT_CONFIGURATION);
                }
                is_sm_process_data_invalid = true;
                break;
            }
        }

        if ((al_control_ & State::MASK_STATE) == State::PRE_OP or is_sm_process_data_invalid)
        {
            set_al_status(State::PRE_OP);
            for (auto& sm : sm_process_data_configs_)
            {
                set_sm_activate(sm, false);
            }
        }
        else if ((al_control_ & State::MASK_STATE) == State::OPERATIONAL)
        {
            if (are_valid_output_data_)
            {
                set_al_status(State::OPERATIONAL);
            }
            else
            {
                // error flag
            }
        }
    }


    void AbstractESC::routine_op()
    {
        if (has_expired_watchdog())
        {
            set_error(StatusCode::SYNC_MANAGER_WATCHDOG);
        }

        if (al_status_ & State::ERROR_ACK)
        {
            // In case of error in OP, go to a lower state, CTT wants safe op; not specified in the norms.
            for (auto& sm : sm_process_data_configs_)
            {
                if (sm.type == SyncManagerType::Output)
                {
                    set_sm_activate(sm, false);
                }
            }
            set_al_status(State::SAFE_OP);
            return;
        }

        update_process_data_input();
        update_process_data_output();
        
        if (not are_valid_sm(sm_mailbox_configs_))
        {
            for (auto& sm : sm_mailbox_configs_)
            {
                set_sm_activate(sm, false);
            }

            for (auto& sm : sm_process_data_configs_)
            {
                set_sm_activate(sm, false);
            }

            set_al_status(State::INIT);
            set_error(StatusCode::INVALID_MAILBOX_CONFIGURATION_PREOP);
        }


        bool is_sm_process_data_invalid = false;
        for (auto& sm : sm_process_data_configs_)
        {
            if (not is_valid_sm(sm))
            {
                if (sm.type == SyncManagerType::Input)
                {
                    set_error(StatusCode::INVALID_INPUT_CONFIGURATION);
                }
                else if (sm.type == SyncManagerType::Output)
                {
                    set_error(StatusCode::INVALID_OUTPUT_CONFIGURATION);
                }
                is_sm_process_data_invalid = true;
                break;
            }
        }

        if ((al_control_ & State::MASK_STATE) == State::PRE_OP or is_sm_process_data_invalid)
        {
            for (auto& sm : sm_process_data_configs_)
            {
                set_sm_activate(sm, false);
            }
            set_al_status(State::PRE_OP);
        }
        else if ((al_control_ & State::MASK_STATE) == State::SAFE_OP)
        {
            set_al_status(State::SAFE_OP);
        }
    }


    void AbstractESC::update_process_data_output()
    {
        int32_t r = read(sm_pd_output_.start_address, process_data_output_, sm_pd_output_.length);
        if (r != sm_pd_output_.length)
        {
            slave_error("\n update_process_data_output ERROR\n");
        }
    }


    void AbstractESC::update_process_data_input()
    {
        int32_t written = write(sm_pd_input_.start_address, process_data_input_, sm_pd_input_.length);
        if (written != sm_pd_input_.length)
        {
            slave_error("\n update_process_data_input ERROR\n");
        }
    }


    void AbstractESC::set_valid_output_data_received(bool are_valid_output)
    {
        are_valid_output_data_ = are_valid_output;
    }


    void AbstractESC::set_state_on_error(State state, StatusCode error_code)
    {
        set_al_status(state);
        set_error(error_code);
    }


    void AbstractESC::clear_error()
    {
        al_status_code_ = StatusCode::NO_ERROR;
        al_status_ &= ~AL_STATUS_ERR_IND;
    }


    void AbstractESC::set_error(StatusCode code)
    {
        // Don't override non acknowlegded error, demanded by CTT, not specified in the norm.
        if (not(al_status_ & State::ERROR_ACK))
        {
            al_status_code_ = code;
            al_status_ |= State::ERROR_ACK;
        }
    }


    void AbstractESC::set_al_status(State state)
    {
        al_status_ = (al_status_ & ~State::MASK_STATE) | state;
    }
}
