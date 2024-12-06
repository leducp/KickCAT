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

    void SlaveState::setMailbox(mailbox::response::Mailbox* mbx)
    {
        mbx_ = mbx;
    }

    State SlaveState::getRequestedState(uint16_t al_control)
    {
        return static_cast<State>(al_control & State::MASK_STATE);
    }

    std::tuple<uint16_t, uint16_t> SlaveState::buildALStatus(uint8_t state, uint8_t statusCode)
    {
        if (statusCode == NO_ERROR)
        {
            return std::tuple(state, statusCode);
        }
        else
        {
            return std::tuple(state | State::ERROR_ACK, statusCode);
        }
    }

    Init::Init(AbstractESC& esc, PDO& pdo)
        : SlaveState(State::INIT, esc, pdo)
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

    std::tuple<uint16_t, uint16_t> Init::routine(uint16_t al_control,
                                                         uint16_t al_status,
                                                         uint16_t al_status_code)
    {
        // If master didn't aknowledge error but asked for the init, aknowledge it
        // If it didn't ask stay in init and don't aknowledge
        if (al_status & ERROR_ACK and not(al_control & ERROR_ACK))
        {
            if (getRequestedState(al_control) == INIT)
            {
                return buildALStatus(INIT, NO_ERROR);
            }
            else
            {
                return buildALStatus(INIT, al_status_code);
            }
        }

        // asked for Next mode ?
        if (getRequestedState(al_control) == State::PRE_OP)
        {
            if (not mbx_)
            {
                return buildALStatus(State::PRE_OP, StatusCode::NO_ERROR);
            }
            if (mbx_->configureSm() == hresult::OK)
            {
                if (mbx_->is_sm_config_ok())
                {
                    printf("go to preop\n");
                    return buildALStatus(State::PRE_OP, StatusCode::NO_ERROR);
                }
                else
                {
                    return buildALStatus(State::INIT, StatusCode::INVALID_MAILBOX_CONFIGURATION_PREOP);
                }
            }
        }

        // Invalid state request
        if ((getRequestedState(al_control) == State::SAFE_OP) or (getRequestedState(al_control) == State::OPERATIONAL))
        {
            return buildALStatus(State::INIT, StatusCode::INVALID_REQUESTED_STATE_CHANGE);
        }

        // Unknown state request
        auto requestedState = getRequestedState(al_control);
        if (requestedState != State::BOOT and requestedState != State::INIT and requestedState != State::PRE_OP
            and requestedState != State::SAFE_OP and requestedState != State::OPERATIONAL)
        {
            return buildALStatus(id_, StatusCode::UNKNOWN_REQUESTED_STATE);
        }

        // BOOTSTRAP not supported yet. If implemented, need to check the SII to know if enabled.
        if (getRequestedState(al_control) == State::BOOT)
        {
            return std::tuple(id_, StatusCode::BOOTSTRAP_NOT_SUPPORTED);
        }

        return std::tuple(State::INIT, StatusCode::NO_ERROR);
    }

    PreOP::PreOP(AbstractESC& esc, PDO& pdo)
        : SlaveState(State::PRE_OP, esc, pdo)
    {
    }

    void PreOP::onEntry(uint8_t)
    {
        printf("Preop::onEntry\n");
        if (mbx_)
        {
            mbx_->set_sm_activate(true);
        }
        pdo_.set_sm_activated(false);
    }

    std::tuple<uint16_t, uint16_t> PreOP::routine(uint16_t al_control,
                                                          uint16_t al_status,
                                                          uint16_t al_status_code)
    {
        if (al_status & ERROR_ACK and not(al_control & ERROR_ACK))
        {
            if (getRequestedState(al_control) == INIT)
            {
                return buildALStatus(INIT, al_status_code);
            }

            return buildALStatus(al_status, al_status_code);
        }

        if (mbx_ and not mbx_->is_sm_config_ok())
        {
            return std::tuple(State::INIT, StatusCode::INVALID_MAILBOX_CONFIGURATION_PREOP);
        }

        if (getRequestedState(al_control) == State::SAFE_OP)
        {
            if (pdo_.configure_pdo_sm() != hresult::OK)
            {
                return std::tuple(id_, StatusCode::NO_ERROR);
            }

            StatusCode pdo_sm_config_status_code = pdo_.is_sm_config_ok();
            if (pdo_sm_config_status_code != StatusCode::NO_ERROR)
            {
                return std::tuple(id_, pdo_sm_config_status_code);
            }

            return std::tuple(State::SAFE_OP, StatusCode::NO_ERROR);
        }

        if (getRequestedState(al_control) == State::INIT)
        {
            return std::tuple(INIT, StatusCode::NO_ERROR);
        }

        if (getRequestedState(al_control) == State::OPERATIONAL or getRequestedState(al_control) == State::BOOT)
        {
            return std::tuple(id_, StatusCode::INVALID_REQUESTED_STATE_CHANGE);
        }

        auto requestedState = getRequestedState(al_control);
        if (requestedState != State::BOOT and requestedState != State::INIT and requestedState != State::PRE_OP
            and requestedState != State::SAFE_OP and requestedState != State::OPERATIONAL)
        {
            return buildALStatus(id_, StatusCode::UNKNOWN_REQUESTED_STATE);
        }

        return std::tuple(id_, StatusCode::NO_ERROR);
    }


    SafeOP::SafeOP(AbstractESC& esc, PDO& pdo)
        : SlaveState(State::SAFE_OP, esc, pdo)
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

    std::tuple<uint16_t, uint16_t> SafeOP::routine(uint16_t al_control,
                                                           uint16_t al_status,
                                                           uint16_t al_status_code)
    {
        if (al_status & ERROR_ACK and not(al_control & ERROR_ACK))
        {
            if (getRequestedState(al_control) == INIT)
            {
                return buildALStatus(INIT, al_status_code);
            }

            return buildALStatus(al_status, al_status_code);
        }

        pdo_.update_process_data_input();
        pdo_.update_process_data_output();

        if (mbx_ and not mbx_->is_sm_config_ok())
        {
            return std::tuple(State::INIT, StatusCode::INVALID_MAILBOX_CONFIGURATION_PREOP);
        }

        StatusCode pdo_sm_config_status_code = pdo_.is_sm_config_ok();
        if (pdo_sm_config_status_code != StatusCode::NO_ERROR)
        {
            return std::tuple(State::PRE_OP, pdo_sm_config_status_code);
        }

        //TODO: check if valid_ output data
        if (getRequestedState(al_control) == State::OPERATIONAL)
        {
            return std::tuple(State::OPERATIONAL, StatusCode::NO_ERROR);
        }
        else if (getRequestedState(al_control) == State::PRE_OP or getRequestedState(al_control) == State::INIT)
        {
            return std::tuple(getRequestedState(al_control), StatusCode::NO_ERROR);
        }

        if (getRequestedState(al_control) == State::BOOT)
        {
            return std::tuple(State::SAFE_OP, INVALID_REQUESTED_STATE_CHANGE);
        }

        auto requestedState = getRequestedState(al_control);
        if (requestedState != State::BOOT and requestedState != State::INIT and requestedState != State::PRE_OP
            and requestedState != State::SAFE_OP and requestedState != State::OPERATIONAL)
        {
            return buildALStatus(State::SAFE_OP, StatusCode::UNKNOWN_REQUESTED_STATE);
        }

        return std::tuple(State::SAFE_OP, NO_ERROR);
    }

    OP::OP(AbstractESC& esc, PDO& pdo)
        : SlaveState(State::OPERATIONAL, esc, pdo)
    {
    }

    std::tuple<uint16_t, uint16_t> OP::routine(uint16_t al_control, uint16_t al_status, uint16_t al_status_code)
    {
        if (al_status & ERROR_ACK and not(al_control & ERROR_ACK))
        {
            if (getRequestedState(al_control) == INIT)
            {
                return buildALStatus(INIT, al_status_code);
            }

            return buildALStatus(al_status, al_status_code);
        }


        pdo_.update_process_data_input();
        pdo_.update_process_data_output();

        if (has_expired_watchdog())
        {
            return std::tuple(State::SAFE_OP, SYNC_MANAGER_WATCHDOG);
        }

        if (mbx_ and not mbx_->is_sm_config_ok())
        {
            return std::tuple(INIT, INVALID_MAILBOX_CONFIGURATION_PREOP);
        }

        if (getRequestedState(al_control) == State::BOOT)
        {
            return std::tuple(State::SAFE_OP, INVALID_REQUESTED_STATE_CHANGE);
        }

        StatusCode pdo_sm_config_status_code = pdo_.is_sm_config_ok();
        if (pdo_sm_config_status_code != StatusCode::NO_ERROR)
        {
            return std::tuple(PRE_OP, pdo_sm_config_status_code);
        }

        if (getRequestedState(al_control) == State::BOOT)
        {
            return std::tuple(SAFE_OP, INVALID_REQUESTED_STATE_CHANGE);
        }

        auto requestedState = getRequestedState(al_control);
        if (requestedState != State::BOOT and requestedState != State::INIT and requestedState != State::PRE_OP
            and requestedState != State::SAFE_OP and requestedState != State::OPERATIONAL)
        {
            return buildALStatus(SAFE_OP, StatusCode::UNKNOWN_REQUESTED_STATE);
        }

        return std::tuple(getRequestedState(al_control), NO_ERROR);
    }

    bool OP::has_expired_watchdog()
    {
        //TODO should be given in entry of the state
        uint16_t watchdog{0};
        esc_.read(reg::WDOG_STATUS, &watchdog, sizeof(watchdog));
        return not(watchdog & 0x1);
    }

}
