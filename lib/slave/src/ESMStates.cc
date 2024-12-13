#include "kickcat/ESMStates.h"
#include "ESM.h"
#include "kickcat/AbstractESC.h"
#include "protocol.h"

namespace kickcat::ESM
{
    Init::Init(AbstractESC& esc, PDO& pdo)
        : AbstractState(State::INIT, esc, pdo)
    {
    }

    void Init::on_entry(Context, Context)
    {
        if (mbx_)
        {
            mbx_->set_sm_activate(false);
        }
        pdo_.set_sm_input_activated(false);
        pdo_.set_sm_output_activated(false);
    }


    Context AbstractState::routine(Context currentStatus, ALControl control)
    {
        // If master didn't aknowledge error
        if (currentStatus.al_status & ERROR_ACK and not(control.value & ERROR_ACK))
        {
            // If in INIT and asked for INIT, clear the error
            if (control.get_requested_state() == INIT)
            {
                return Context::build(INIT);
            }

            // If didn't asked asked for INIT stay where it is and don't do anything
            return Context::build(currentStatus.al_status, currentStatus.al_status_code);
        }

        // Unknown state request
        auto requestedState = control.get_requested_state();
        if (currentStatus.get_state() != State::OPERATIONAL and requestedState != State::BOOT
            and requestedState != State::INIT and requestedState != State::PRE_OP and requestedState != State::SAFE_OP
            and requestedState != State::OPERATIONAL)
        {
            return Context::build(id_, StatusCode::UNKNOWN_REQUESTED_STATE);
        }

        return routine_internal(currentStatus, control);
    }

    Context Init::routine_internal(Context, ALControl control)
    {
        if (control.get_requested_state() == State::PRE_OP)
        {
            if (not mbx_)
            {
                return Context::build(State::PRE_OP);
            }
            if (mbx_->configureSm() == hresult::OK)
            {
                if (mbx_->is_sm_config_ok())
                {
                    return Context::build(State::PRE_OP);
                }
                else
                {
                    return Context::build(State::INIT, StatusCode::INVALID_MAILBOX_CONFIGURATION_PREOP);
                }
            }
        }

        // Invalid state request
        if ((control.get_requested_state() == State::SAFE_OP) or (control.get_requested_state() == State::OPERATIONAL))
        {
            return Context::build(State::INIT, StatusCode::INVALID_REQUESTED_STATE_CHANGE);
        }

        // BOOTSTRAP not supported yet. If implemented, need to check the SII to know if enabled.
        if (control.get_requested_state() == State::BOOT)
        {
            return Context::build(id_, StatusCode::BOOTSTRAP_NOT_SUPPORTED);
        }

        return Context::build(State::INIT);
    }

    PreOP::PreOP(AbstractESC& esc, PDO& pdo)
        : AbstractState(State::PRE_OP, esc, pdo)
    {
    }

    void PreOP::on_entry(Context, Context)
    {
        if (mbx_)
        {
            mbx_->set_sm_activate(true);
        }
        pdo_.set_sm_output_activated(false);
        pdo_.set_sm_input_activated(false);
    }

    Context PreOP::routine_internal(Context, ALControl control)
    {
        if (mbx_ and not mbx_->is_sm_config_ok())
        {
            return Context::build(State::INIT, StatusCode::INVALID_MAILBOX_CONFIGURATION_PREOP);
        }

        if (control.get_requested_state() == State::SAFE_OP)
        {
            if (pdo_.configure_pdo_sm() != hresult::OK)
            {
                return Context::build(id_);
            }

            StatusCode pdo_sm_config_status_code = pdo_.is_sm_config_ok();
            if (pdo_sm_config_status_code != StatusCode::NO_ERROR)
            {
                return Context::build(id_, pdo_sm_config_status_code);
            }

            return Context::build(State::SAFE_OP);
        }

        if (control.get_requested_state() == State::INIT)
        {
            return Context::build(INIT);
        }

        if (control.get_requested_state() == State::OPERATIONAL or control.get_requested_state() == State::BOOT)
        {
            return Context::build(id_, StatusCode::INVALID_REQUESTED_STATE_CHANGE);
        }

        return Context::build(id_);
    }


    SafeOP::SafeOP(AbstractESC& esc, PDO& pdo)
        : AbstractState(State::SAFE_OP, esc, pdo)
    {
    }

    void SafeOP::on_entry(Context oldStatus, Context newStatus)
    {
        if (oldStatus.get_state() == State::OPERATIONAL and newStatus.al_status_code != StatusCode::NO_ERROR)
        {
            pdo_.set_sm_output_activated(false);
        }
        else
        {
            pdo_.set_sm_output_activated(true);
            pdo_.set_sm_input_activated(true);
        }
    }

    Context SafeOP::routine_internal(Context currentStatus, ALControl control)
    {
        pdo_.update_process_data_input();
        pdo_.update_process_data_output();

        if (mbx_ and not mbx_->is_sm_config_ok())
        {
            return Context::build(State::INIT, StatusCode::INVALID_MAILBOX_CONFIGURATION_PREOP);
        }

        StatusCode pdo_sm_config_status_code = pdo_.is_sm_config_ok();
        if (pdo_sm_config_status_code != StatusCode::NO_ERROR)
        {
            return Context::build(State::PRE_OP, pdo_sm_config_status_code);
        }

        if (control.get_requested_state() == State::OPERATIONAL and currentStatus.validOutputData)
        {
            return Context::build(State::OPERATIONAL);
        }

        if (control.get_requested_state() == State::PRE_OP or control.get_requested_state() == State::INIT)
        {
            return Context::build(control.get_requested_state());
        }

        if (control.get_requested_state() == State::BOOT)
        {
            return Context::build(State::SAFE_OP, INVALID_REQUESTED_STATE_CHANGE);
        }

        return Context::build(State::SAFE_OP);
    }

    OP::OP(AbstractESC& esc, PDO& pdo)
        : AbstractState(State::OPERATIONAL, esc, pdo)
    {
    }

    Context OP::routine_internal(Context currentStatus, ALControl control)
    {
        pdo_.update_process_data_input();
        pdo_.update_process_data_output();

        if (currentStatus.has_expired_watchdog())
        {
            return Context::build(State::SAFE_OP, SYNC_MANAGER_WATCHDOG);
        }

        if (mbx_ and not mbx_->is_sm_config_ok())
        {
            return Context::build(INIT, INVALID_MAILBOX_CONFIGURATION_PREOP);
        }

        StatusCode pdo_sm_config_status_code = pdo_.is_sm_config_ok();
        if (pdo_sm_config_status_code != StatusCode::NO_ERROR)
        {
            return Context::build(PRE_OP, pdo_sm_config_status_code);
        }

        if (control.get_requested_state() == State::BOOT)
        {
            return Context::build(State::SAFE_OP, INVALID_REQUESTED_STATE_CHANGE);
        }

        if (control.get_requested_state() == State::PRE_OP or control.get_requested_state() == State::INIT
            or control.get_requested_state() == State::SAFE_OP)
        {
            return Context::build(control.get_requested_state());
        }

        auto requestedState = control.get_requested_state();
        if (requestedState != State::BOOT and requestedState != State::INIT and requestedState != State::PRE_OP
            and requestedState != State::SAFE_OP and requestedState != State::OPERATIONAL)
        {
            return Context::build(State::SAFE_OP, StatusCode::UNKNOWN_REQUESTED_STATE);
        }

        return Context::build(control.get_requested_state());
    }

}
