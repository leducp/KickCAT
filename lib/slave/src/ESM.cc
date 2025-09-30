#include "kickcat/ESM.h"
#include <algorithm>
#include "protocol.h"

namespace kickcat::ESM
{
    AbstractState::AbstractState(uint8_t id, AbstractESC& esc, PDO& pdo)
        : id_(id)
        , esc_{esc}
        , pdo_{pdo}
    {
    }

    void AbstractState::setMailbox(mailbox::response::Mailbox* mbx)
    {
        mbx_ = mbx;
    }

    void AbstractState::onEntry(Context, Context) {};

    uint8_t AbstractState::id()
    {
        return id_;
    }

    StateMachine::StateMachine(AbstractESC& esc, std::array<ESM::AbstractState*, NUMBER_OF_STATES>&& states)
        : esc_{esc}
        , states_{std::move(states)}
    {
        current_state_ = states_[0];
    }

    void StateMachine::validateOutputData()
    {
        status_.is_valid_output_data = true;
    }

    State StateMachine::state()
    {
        return status_.state();
    }

    void StateMachine::start()
    {
        current_state_->onEntry(status_, status_);
    }

    AbstractState* StateMachine::findState(uint8_t id)
    {
        auto it = std::find_if(std::begin(states_), std::end(states_), [&](auto* state) { return state->id() == id; });
        if (it == states_.end())
        {
            return nullptr;
        }

        return *it;
    }

    void StateMachine::play()
    {
        // Get al control
        ALControl control;
        esc_.read(reg::AL_CONTROL, &control.value, sizeof(control.value));

        // Update watchdog
        esc_.read(reg::WDOG_STATUS, &status_.al_watchdog_process_data, sizeof(status_.al_watchdog_process_data));

        auto newStatus = current_state_->routine(status_, control);

        uint8_t newStateId = newStatus.state();

        if (newStateId != current_state_->id())
        {
            AbstractState* newState = findState(newStateId);
            if (not newState)
            {
                newState            = states_[0];
                newStatus.al_status = newState->id();
                newStatus.al_status_code = StatusCode::UNKNOWN_REQUESTED_STATE;
            }

            if (newState != current_state_)
            {
                newState->onEntry(status_, newStatus);
                current_state_ = newState;
            }
        }

        if (status_.al_status != newStatus.al_status or status_.al_status_code != newStatus.al_status_code)
        {
            status_ = newStatus;

            // Note : StatusCode MUST be set before status
            esc_.write(reg::AL_STATUS_CODE, &status_.al_status_code, sizeof(status_.al_status_code));
            esc_.write(reg::AL_STATUS, &status_.al_status, sizeof(status_.al_status));
        }
    }
}
