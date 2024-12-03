#include "kickcat/SlaveFSM.h"
#include "FSM.h"
#include "kickcat/AbstractESC.h"
#include "protocol.h"

namespace kickcat::FSM
{

    SlaveState::SlaveState(uint8_t id, AbstractESC& esc, PDO& pdo)
        : AbstractState(id)
        , esc_{esc}
        , pdo_{pdo}
    {
    }

    void SlaveState::startRoutine()
    {
        esc_.read(reg::AL_CONTROL, &al_control_, sizeof(al_control_));
        esc_.read(reg::AL_STATUS, &al_status_, sizeof(al_status_));

        if ((al_control_ & State::ERROR_ACK))
        {
            clear_error();
        }

        // Do nothing until error is acknowledged by master.
        if (al_status_ & State::ERROR_ACK)
        {
            return;
        }

        routine();
    }

    void SlaveState::endRoutine()
    {
        esc_.write(reg::AL_STATUS, &al_status_, sizeof(al_status_));
        esc_.write(reg::AL_STATUS_CODE, &al_status_code_, sizeof(al_status_code_));
    }

    void SlaveState::onEntry()
    {
        set_al_status(static_cast<kickcat::State>(id_));

        onEntryInternal();
    }

    void SlaveState::setMailbox(mailbox::response::Mailbox* mbx)
    {
        mbx_ = mbx;
    }

    void SlaveState::set_al_status(State state)
    {
        al_status_ = (al_status_ & ~State::MASK_STATE) | state;
    }

    State SlaveState::getRequestedState()
    {
        return static_cast<State>(al_control_ & kickcat::State::MASK_STATE);
    }

    void SlaveState::clear_error()
    {
        al_status_code_ = StatusCode::NO_ERROR;
        al_status_ &= ~AL_STATUS_ERR_IND;
    }

    void SlaveState::set_error(StatusCode code)
    {
        // Don't override non acknowlegded error, demanded by CTT, not specified in the norm.
        if (not(al_status_ & State::ERROR_ACK))
        {
            al_status_code_ = code;
            al_status_ |= State::ERROR_ACK;
        }
    }


    Init::Init(AbstractESC& esc, PDO& pdo)
        : SlaveState(kickcat::State::INIT, esc, pdo)
    {
    }

    void Init::onEntryInternal()
    {
        clear_error();
        if (mbx_)
        {
            mbx_->set_sm_activate(false);
        }
        pdo_.set_sm_activated(false);
    }

    uint8_t Init::transition()
    {
        if (getRequestedState() == kickcat::State::PRE_OP)
        {
            if (not mbx_)
            {
                return kickcat::State::PRE_OP;
            }
            if (mbx_->configureSm() == hresult::OK)
            {
                if (mbx_->is_sm_config_ok())
                {
                    return kickcat::State::PRE_OP;
                }
                else
                {
                    set_error(StatusCode::INVALID_MAILBOX_CONFIGURATION_PREOP);
                }
            }
        }

        if ((getRequestedState() == State::SAFE_OP) or (getRequestedState() == State::OPERATIONAL))
        {
            set_error(StatusCode::INVALID_REQUESTED_STATE_CHANGE);
        }

        return kickcat::State::INIT;
    }

    PreOP::PreOP(AbstractESC& esc, PDO& pdo)
        : SlaveState(kickcat::State::PRE_OP, esc, pdo)
    {
    }

    void PreOP::onEntryInternal()
    {
        if (mbx_)
        {
            mbx_->set_sm_activate(true);
        }
        pdo_.set_sm_activated(false);
    }

    uint8_t PreOP::transition()
    {
        if (mbx_ and not mbx_->is_sm_config_ok())
        {
            set_error(StatusCode::INVALID_MAILBOX_CONFIGURATION_PREOP);
            return kickcat::State::INIT;
        }

        if (getRequestedState() == kickcat::State::SAFE_OP and not(al_status_ & State::ERROR_ACK))
        {
            if (pdo_.configure_pdo_sm() != hresult::OK)
            {
                return kickcat::State::PRE_OP;
            }

            StatusCode pdo_sm_config_status_code = pdo_.is_sm_config_ok();
            if (pdo_sm_config_status_code != NO_ERROR)
            {
                set_error(pdo_sm_config_status_code);
                return kickcat::State::PRE_OP;
            }

            return kickcat::State::SAFE_OP;
        }

        if (getRequestedState() == State::INIT)
        {
            return State::INIT;
        }
        else if (getRequestedState() == State::OPERATIONAL)
        {
            set_error(StatusCode::INVALID_REQUESTED_STATE_CHANGE);
        }

        return kickcat::State::PRE_OP;
    }


    SafeOP::SafeOP(AbstractESC& esc, PDO& pdo)
        : SlaveState(kickcat::State::SAFE_OP, esc, pdo)
    {
    }


    void SafeOP::onEntryInternal()
    {
        pdo_.set_sm_activated(true);
    }

    void SafeOP::routine()
    {
        printf("SafeOP ROutine !!\n");
        pdo_.update_process_data_input();
        pdo_.update_process_data_output();
    }

    uint8_t SafeOP::transition()
    {
        if (mbx_ and not mbx_->is_sm_config_ok())
        {
            set_error(StatusCode::INVALID_MAILBOX_CONFIGURATION_PREOP);
            return State::INIT;
        }

        StatusCode pdo_sm_config_status_code = pdo_.is_sm_config_ok();
        if (pdo_sm_config_status_code != NO_ERROR)
        {
            set_error(pdo_sm_config_status_code);
            return kickcat::State::PRE_OP;
        }

        //TODO: check if valid_ output data
        if (getRequestedState() == kickcat::State::OPERATIONAL)
        {
            return kickcat::State::OPERATIONAL;
        }
        else if (getRequestedState() == State::PRE_OP or getRequestedState() == State::INIT)
        {
            return getRequestedState();
        }

        return kickcat::State::SAFE_OP;
    }

    OP::OP(AbstractESC& esc, PDO& pdo)
        : SlaveState(kickcat::State::OPERATIONAL, esc, pdo)
    {
    }

    void OP::routine()
    {
        pdo_.update_process_data_input();
        pdo_.update_process_data_output();
    }

    uint8_t OP::transition()
    {
        if (has_expired_watchdog())
        {
            set_error(StatusCode::SYNC_MANAGER_WATCHDOG);
        }

        if (al_status_ & State::ERROR_ACK)
        {
            // In case of error in OP, go to a lower state, CTT wants safe op; not specified in the norms.
            return State::SAFE_OP;
        }

        if (mbx_ and not mbx_->is_sm_config_ok())
        {
            set_error(StatusCode::INVALID_MAILBOX_CONFIGURATION_PREOP);
            return State::INIT;
        }

        StatusCode pdo_sm_config_status_code = pdo_.is_sm_config_ok();
        if (pdo_sm_config_status_code != NO_ERROR)
        {
            set_error(pdo_sm_config_status_code);
            return kickcat::State::PRE_OP;
        }

        return getRequestedState();
    }

    bool OP::has_expired_watchdog()
    {
        uint16_t watchdog{0};
        esc_.read(reg::WDOG_STATUS, &watchdog, sizeof(watchdog));
        return not(watchdog & 0x1);
    }

}
