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

    void Init::onEntry(Context, Context)
    {
        if (mbx_)
        {
            mbx_->activate(false); // reset dictionary here?
        }
        pdo_.activateInput(false);
        pdo_.activateOuput(false);
    }


    Context AbstractState::routine(Context currentStatus, ALControl control)
    {
        // If master didn't aknowledge error
        if (currentStatus.al_status & ERROR_ACK and not(control.value & ERROR_ACK))
        {
            // If in INIT and asked for INIT, clear the error
            if (control.requestedState() == INIT)
            {
                return Context::build(INIT);
            }

            // If didn't asked asked for INIT stay where it is and don't do anything
            return Context::build(currentStatus.al_status, currentStatus.al_status_code);
        }

        // Unknown state request
        auto requestedState = control.requestedState();
        if (currentStatus.state() != State::OPERATIONAL and requestedState != State::BOOT
            and requestedState != State::INIT and requestedState != State::PRE_OP and requestedState != State::SAFE_OP
            and requestedState != State::OPERATIONAL)
        {
            return Context::build(id_, StatusCode::UNKNOWN_REQUESTED_STATE);
        }

        return routineInternal(currentStatus, control);
    }

    Context Init::routineInternal(Context, ALControl control)
    {
        if (control.requestedState() == State::PRE_OP)
        {
            if (not mbx_)
            {
                return Context::build(State::PRE_OP);
            }
            if (mbx_->configure() == hresult::OK)
            {
                if (mbx_->isConfigOk())
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
        if ((control.requestedState() == State::SAFE_OP) or (control.requestedState() == State::OPERATIONAL))
        {
            return Context::build(State::INIT, StatusCode::INVALID_REQUESTED_STATE_CHANGE);
        }

        // BOOTSTRAP not supported yet. If implemented, need to check the SII to know if enabled.
        if (control.requestedState() == State::BOOT)
        {
            return Context::build(id_, StatusCode::BOOTSTRAP_NOT_SUPPORTED);
        }

        return Context::build(State::INIT);
    }

    PreOP::PreOP(AbstractESC& esc, PDO& pdo)
        : AbstractState(State::PRE_OP, esc, pdo)
    {
    }

    void PreOP::onEntry(Context, Context)
    {
        if (mbx_)
        {
            mbx_->activate(true);
        }
        pdo_.activateOuput(false);
        pdo_.activateInput(false);
    }

    Context PreOP::routineInternal(Context, ALControl control)
    {
        if (mbx_ and not mbx_->isConfigOk())
        {
            return Context::build(State::INIT, StatusCode::INVALID_MAILBOX_CONFIGURATION_PREOP);
        }

        if (control.requestedState() == State::SAFE_OP)
        {
            if (pdo_.configure() != hresult::OK)
            {
                return Context::build(id_);
            }

            StatusCode pdo_sm_config_status_code = pdo_.isConfigOk();
            if (pdo_sm_config_status_code != StatusCode::NO_ERROR)
            {
                return Context::build(id_, pdo_sm_config_status_code);
            }

            return Context::build(State::SAFE_OP);
        }

        if (control.requestedState() == State::INIT)
        {
            return Context::build(INIT);
        }

        if (control.requestedState() == State::OPERATIONAL or control.requestedState() == State::BOOT)
        {
            return Context::build(id_, StatusCode::INVALID_REQUESTED_STATE_CHANGE);
        }

        return Context::build(id_);
    }


    SafeOP::SafeOP(AbstractESC& esc, PDO& pdo)
        : AbstractState(State::SAFE_OP, esc, pdo)
    {
    }

    void SafeOP::onEntry(Context oldStatus, Context newStatus)
    {
        if (oldStatus.state() == State::OPERATIONAL and newStatus.al_status_code != StatusCode::NO_ERROR)
        {
            pdo_.activateOuput(false);
        }
        else
        {
            pdo_.activateOuput(true);
            pdo_.activateInput(true);
        }
    }

    Context SafeOP::routineInternal(Context currentStatus, ALControl control)
    {
        pdo_.updateInput();
        pdo_.updateOutput();

        if (mbx_ and not mbx_->isConfigOk())
        {
            return Context::build(State::INIT, StatusCode::INVALID_MAILBOX_CONFIGURATION_PREOP);
        }

        StatusCode pdo_sm_config_status_code = pdo_.isConfigOk();
        if (pdo_sm_config_status_code != StatusCode::NO_ERROR)
        {
            return Context::build(State::PRE_OP, pdo_sm_config_status_code);
        }

        if (control.requestedState() == State::OPERATIONAL and currentStatus.is_valid_output_data)
        {
            return Context::build(State::OPERATIONAL);
        }

        if (control.requestedState() == State::PRE_OP or control.requestedState() == State::INIT)
        {
            return Context::build(control.requestedState());
        }

        if (control.requestedState() == State::BOOT)
        {
            return Context::build(State::SAFE_OP, INVALID_REQUESTED_STATE_CHANGE);
        }

        return Context::build(State::SAFE_OP);
    }

    OP::OP(AbstractESC& esc, PDO& pdo)
        : AbstractState(State::OPERATIONAL, esc, pdo)
    {
    }

    Context OP::routineInternal(Context currentStatus, ALControl control)
    {
        pdo_.updateInput();
        pdo_.updateOutput();

        if (currentStatus.hasExpiredWatchdog())
        {
            return Context::build(State::SAFE_OP, SYNC_MANAGER_WATCHDOG);
        }

        if (mbx_ and not mbx_->isConfigOk())
        {
            return Context::build(INIT, INVALID_MAILBOX_CONFIGURATION_PREOP);
        }

        StatusCode pdo_sm_config_status_code = pdo_.isConfigOk();
        if (pdo_sm_config_status_code != StatusCode::NO_ERROR)
        {
            return Context::build(PRE_OP, pdo_sm_config_status_code);
        }

        if (control.requestedState() == State::BOOT)
        {
            return Context::build(State::SAFE_OP, INVALID_REQUESTED_STATE_CHANGE);
        }

        if (control.requestedState() == State::PRE_OP or control.requestedState() == State::INIT
            or control.requestedState() == State::SAFE_OP)
        {
            return Context::build(control.requestedState());
        }

        auto requestedState = control.requestedState();
        if (requestedState != State::BOOT and requestedState != State::INIT and requestedState != State::PRE_OP
            and requestedState != State::SAFE_OP and requestedState != State::OPERATIONAL)
        {
            return Context::build(State::SAFE_OP, StatusCode::UNKNOWN_REQUESTED_STATE);
        }

        return Context::build(control.requestedState());
    }

}
