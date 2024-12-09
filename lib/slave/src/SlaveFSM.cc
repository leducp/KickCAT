#include "kickcat/SlaveFSM.h"
#include "FSM.h"
#include "kickcat/AbstractESC.h"
#include "protocol.h"

namespace kickcat::FSM
{
    Init::Init(AbstractESC& esc, PDO& pdo)
        : AbstractState(State::INIT, esc, pdo)
    {
    }

    void Init::onEntry(uint8_t)
    {
        if (mbx_)
        {
            mbx_->set_sm_activate(false);
        }
        pdo_.set_sm_activated(false);
    }


    ALStatus AbstractState::routine(ALStatus currentStatus, ALControl control)
    {

        // TODO: update the comment
        // If master didn't aknowledge error but asked for the init, aknowledge it
        // If it didn't ask stay in init and don't aknowledge
        if (currentStatus.al_status & ERROR_ACK and not(control.value & ERROR_ACK))
        {
            if (control.getRequestedState() == INIT and currentStatus.getState() == INIT)
            {
                return ALStatus::build(INIT);
            }

            if (control.getRequestedState() == INIT)
            {
                return ALStatus::build(INIT, currentStatus.al_status_code);
            }

            return ALStatus::build(currentStatus.al_status, currentStatus.al_status_code);
        }

        // Unknown state request
        auto requestedState = control.getRequestedState();
        if (currentStatus.getState() != State::OPERATIONAL and requestedState != State::BOOT and requestedState != State::INIT
            and requestedState != State::PRE_OP and requestedState != State::SAFE_OP
            and requestedState != State::OPERATIONAL)
        {
            return ALStatus::build(id_, StatusCode::UNKNOWN_REQUESTED_STATE);
        }

        return routineInternal(currentStatus, control);
    }

    ALStatus Init::routineInternal(ALStatus, ALControl control)
    {
        if (control.getRequestedState() == State::PRE_OP)
        {
            if (not mbx_)
            {
                return ALStatus::build(State::PRE_OP);
            }
            if (mbx_->configureSm() == hresult::OK)
            {
                if (mbx_->is_sm_config_ok())
                {
                    return ALStatus::build(State::PRE_OP);
                }
                else
                {
                    return ALStatus::build(State::INIT, StatusCode::INVALID_MAILBOX_CONFIGURATION_PREOP);
                }
            }
        }

        // Invalid state request
        if ((control.getRequestedState() == State::SAFE_OP) or (control.getRequestedState() == State::OPERATIONAL))
        {
            return ALStatus::build(State::INIT, StatusCode::INVALID_REQUESTED_STATE_CHANGE);
        }

        // BOOTSTRAP not supported yet. If implemented, need to check the SII to know if enabled.
        if (control.getRequestedState() == State::BOOT)
        {
            return ALStatus::build(id_, StatusCode::BOOTSTRAP_NOT_SUPPORTED);
        }

        return ALStatus::build(State::INIT);
    }

    PreOP::PreOP(AbstractESC& esc, PDO& pdo)
        : AbstractState(State::PRE_OP, esc, pdo)
    {
    }

    void PreOP::onEntry(uint8_t)
    {
        if (mbx_)
        {
            mbx_->set_sm_activate(true);
        }
        pdo_.set_sm_activated(false);
    }

    ALStatus PreOP::routineInternal(ALStatus, ALControl control)
    {
        if (mbx_ and not mbx_->is_sm_config_ok())
        {
            return ALStatus::build(State::INIT, StatusCode::INVALID_MAILBOX_CONFIGURATION_PREOP);
        }

        if (control.getRequestedState() == State::SAFE_OP)
        {
            if (pdo_.configure_pdo_sm() != hresult::OK)
            {
                return ALStatus::build(id_);
            }

            StatusCode pdo_sm_config_status_code = pdo_.is_sm_config_ok();
            if (pdo_sm_config_status_code != StatusCode::NO_ERROR)
            {
                return ALStatus::build(id_, pdo_sm_config_status_code);
            }

            return ALStatus::build(State::SAFE_OP);
        }

        if (control.getRequestedState() == State::INIT)
        {
            return ALStatus::build(INIT);
        }

        if (control.getRequestedState() == State::OPERATIONAL or control.getRequestedState() == State::BOOT)
        {
            return ALStatus::build(id_, StatusCode::INVALID_REQUESTED_STATE_CHANGE);
        }

        return ALStatus::build(id_);
    }


    SafeOP::SafeOP(AbstractESC& esc, PDO& pdo)
        : AbstractState(State::SAFE_OP, esc, pdo)
    {
    }

    void SafeOP::onEntry(uint8_t fromState)
    {
        if (fromState == State::OPERATIONAL)
        {
            pdo_.set_sm_output_activated(false);
        }
        else
        {
            pdo_.set_sm_activated(true);
        }
    }

    ALStatus SafeOP::routineInternal(ALStatus currentStatus, ALControl control)
    {
        pdo_.update_process_data_input();
        pdo_.update_process_data_output();

        if (mbx_ and not mbx_->is_sm_config_ok())
        {
            return ALStatus::build(State::INIT, StatusCode::INVALID_MAILBOX_CONFIGURATION_PREOP);
        }

        StatusCode pdo_sm_config_status_code = pdo_.is_sm_config_ok();
        if (pdo_sm_config_status_code != StatusCode::NO_ERROR)
        {
            return ALStatus::build(State::PRE_OP, pdo_sm_config_status_code);
        }

        if (control.getRequestedState() == State::OPERATIONAL and currentStatus.validOutputData)
        {
            return ALStatus::build(State::OPERATIONAL);
        }
        else if (control.getRequestedState() == State::PRE_OP or control.getRequestedState() == State::INIT)
        {
            return ALStatus::build(control.getRequestedState());
        }

        if (control.getRequestedState() == State::BOOT)
        {
            return ALStatus::build(State::SAFE_OP, INVALID_REQUESTED_STATE_CHANGE);
        }

        return ALStatus::build(State::SAFE_OP);
    }

    OP::OP(AbstractESC& esc, PDO& pdo)
        : AbstractState(State::OPERATIONAL, esc, pdo)
    {
    }

    ALStatus OP::routineInternal(ALStatus currentStatus, ALControl control)
    {
        pdo_.update_process_data_input();
        pdo_.update_process_data_output();

        if (currentStatus.has_expired_watchdog())
        {
            return ALStatus::build(State::SAFE_OP, SYNC_MANAGER_WATCHDOG);
        }

        if (mbx_ and not mbx_->is_sm_config_ok())
        {
            return ALStatus::build(INIT, INVALID_MAILBOX_CONFIGURATION_PREOP);
        }

        StatusCode pdo_sm_config_status_code = pdo_.is_sm_config_ok();
        if (pdo_sm_config_status_code != StatusCode::NO_ERROR)
        {
            return ALStatus::build(PRE_OP, pdo_sm_config_status_code);
        }

        if (control.getRequestedState() == State::BOOT)
        {
            return ALStatus::build(State::SAFE_OP, INVALID_REQUESTED_STATE_CHANGE);
        }

        auto requestedState = control.getRequestedState();
        if (requestedState != State::BOOT and requestedState != State::INIT and requestedState != State::PRE_OP
            and requestedState != State::SAFE_OP and requestedState != State::OPERATIONAL)
        {
            return ALStatus::build(State::SAFE_OP, StatusCode::UNKNOWN_REQUESTED_STATE);
        }



        return ALStatus::build(control.getRequestedState());
    }

}
