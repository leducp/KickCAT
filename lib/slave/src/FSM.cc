#include "kickcat/FSM.h"
#include <algorithm>
#include "protocol.h"

namespace kickcat::FSM
{
    AbstractState::AbstractState(uint8_t id)
        : id_{id}
    {
    }

    uint8_t AbstractState::id()
    {
        return id_;
    }

    void AbstractState::onEntry(uint8_t) {};

    StateMachine::StateMachine(AbstractESC& esc, std::array<FSM::AbstractState*, 4>&& states)
        : esc_{esc}
        , states_{std::move(states)}
    {
        currentState_ = states_[0];
    }

    void StateMachine::start()
    {
        currentState_->onEntry(currentState_->id());
    }

    AbstractState* StateMachine::getState(uint8_t id)
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
        uint16_t al_control = {0};

        // Get al control
        esc_.read(reg::AL_CONTROL, &al_control, sizeof(al_control));

        auto [al_status, al_status_code] = currentState_->routine(al_control, al_status_, al_status_code_);

        al_status_      = al_status;
        al_status_code_ = al_status_code;

        uint8_t newStateId = al_status & State::MASK_STATE;

        if (newStateId != currentState_->id())
        {
            AbstractState* newState = getState(newStateId);
            if (not newState)
            {
                newState = states_[0];
            }

            if (newState != currentState_)
            {
                newState->onEntry(currentState_->id());
                currentState_ = newState;
            }
        }

        // TODO: we shouldn't do that every loop
        esc_.write(reg::AL_STATUS_CODE, &al_status_code_, sizeof(al_status_code_));
        esc_.write(reg::AL_STATUS, &al_status_, sizeof(al_status_));
    }
}
