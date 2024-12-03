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
        //esc_.read(reg::WDOG_STATUS, &watchdog_, sizeof(watchdog_));

        routine();
    }

    void SlaveState::endRoutine()
    {
        esc_.write(reg::AL_STATUS, &al_status_, sizeof(al_status_));
        esc_.write(reg::AL_STATUS_CODE, &al_status_code_, sizeof(al_status_code_));
    }

    void SlaveState::onEntry(uint8_t oldState)
    {
        set_al_status(static_cast<kickcat::State>(id_));

        onEntryInternal(oldState);
    }

    void SlaveState::onExit(uint8_t newState)
    {
        onExitInternal(newState);
    }

    void SlaveState::setMailbox(mailbox::response::Mailbox* mbx)
    {
        mbx_ = mbx;
    }

    void SlaveState::set_al_status(State state)
    {
        al_status_ = (al_status_ & ~State::MASK_STATE) | state;
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

    void Init::routine()
    {
        printf("init routine\n");
    }

    uint8_t Init::transition()
    {
        uint32_t al_control;
        esc_.read(reg::AL_CONTROL, &al_control, sizeof(al_control));
        if ((al_control & kickcat::State::MASK_STATE) == kickcat::State::PRE_OP)
        {
            if (not mbx_)
            {
                return kickcat::State::PRE_OP;
            }
            if (mbx_->configureSm() == hresult::OK)
            {
                if (mbx_->is_sm_config_ok())
                {
                    //TODO: set_sm_activate
                    return kickcat::State::PRE_OP;
                }
                else
                {
                    set_error(StatusCode::INVALID_MAILBOX_CONFIGURATION_PREOP);
                }
            }
        }
        return kickcat::State::INIT;
    }

    void Init::onEntryInternal(uint8_t oldState)
    {
    }

    void Init::onExitInternal(uint8_t newState)
    {
    }

    PreOP::PreOP(AbstractESC& esc, PDO& pdo)
        : SlaveState(kickcat::State::PRE_OP, esc, pdo)
    {
    }

    void PreOP::routine()
    {
        printf("preop routine\n");
    }

    uint8_t PreOP::transition()
    {
        if ((al_control_ & kickcat::State::MASK_STATE) == kickcat::State::SAFE_OP)
        {
            if (pdo_.configure_pdo_sm() != hresult::OK)
            {
                printf("not ok\n");
                return kickcat::State::PRE_OP;
            }

            if (not pdo_.is_sm_config_ok())
            {
                printf("CONFIG not ok\n");
                // TODO: disctinct between input and output
                set_error(StatusCode::INVALID_INPUT_CONFIGURATION);
                // set_error(StatusCode::INVALID_OUTPUT_CONFIGURATION);
                return kickcat::State::PRE_OP;
            }

            printf("OK\n");
            return kickcat::State::SAFE_OP;
        }

        return kickcat::State::PRE_OP;
    }

    void PreOP::onEntryInternal(uint8_t oldState)
    {
        uint32_t al_status = kickcat::State::PRE_OP;
        esc_.write(reg::AL_STATUS, &al_status, sizeof(al_status));
    }

    void PreOP::onExitInternal(uint8_t newState)
    {
    }

    SafeOP::SafeOP(AbstractESC& esc, PDO& pdo)
        : SlaveState(kickcat::State::SAFE_OP, esc, pdo)
    {
    }

    void SafeOP::routine()
    {
        printf("SafeOP ROutine !!\n");
        pdo_.update_process_data_input();
        pdo_.update_process_data_output();
    }

    uint8_t SafeOP::transition()
    {
        //TODO: check if valid_ output data
        if ((al_control_ & kickcat::State::MASK_STATE) == kickcat::State::OPERATIONAL)
        {
            return kickcat::State::OPERATIONAL;
        }
        return kickcat::State::SAFE_OP;
    }

    void SafeOP::onEntryInternal(uint8_t oldState)
    {
        uint32_t al_status = kickcat::State::PRE_OP;
        esc_.write(reg::AL_STATUS, &al_status, sizeof(al_status));
    }

    void SafeOP::onExitInternal(uint8_t newState)
    {
    }

    OP::OP(AbstractESC& esc, PDO& pdo)
        : SlaveState(kickcat::State::OPERATIONAL, esc, pdo)
    {
    }

    void OP::routine()
    {
        printf("OP Routine !!\n");
        pdo_.update_process_data_input();
        pdo_.update_process_data_output();
    }

    uint8_t OP::transition()
    {
        return kickcat::State::OPERATIONAL;
    }

    void OP::onEntryInternal(uint8_t oldState)
    {
        uint32_t al_status = kickcat::State::PRE_OP;
        esc_.write(reg::AL_STATUS, &al_status, sizeof(al_status));
    }

    void OP::onExitInternal(uint8_t newState)
    {
    }
}
