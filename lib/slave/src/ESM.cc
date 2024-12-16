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

    void AbstractState::set_mailbox(mailbox::response::Mailbox* mbx)
    {
        mbx_ = mbx;
    }

    uint8_t AbstractState::id()
    {
        return id_;
    }
    void AbstractState::on_entry(Context, Context){};

    StateMachine::StateMachine(AbstractESC& esc, std::array<ESM::AbstractState*, 4>&& states)
        : esc_{esc}
        , states_{std::move(states)}
    {
        current_state_ = states_[0];
    }

    void StateMachine::validateOutputData()
    {
        status_.validOutputData = true;
    }

    State StateMachine::get_state()
    {
        return status_.get_state();
    }

    void StateMachine::start()
    {
        current_state_->on_entry(status_, status_);
    }

    AbstractState* StateMachine::find_state(uint8_t id)
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

        uint8_t newStateId = newStatus.get_state();

        if (newStateId != current_state_->id())
        {
            AbstractState* newState = find_state(newStateId);
            if (not newState)
            {
                newState            = states_[0];
                newStatus.al_status = newState->id();
            }

            if (newState != current_state_)
            {
                newState->on_entry(status_, newStatus);
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
