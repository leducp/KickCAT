#include "kickcat/FSM.h"
#include <algorithm>
#include "AbstractESC2.h"

namespace kickcat::FSM
{
    AbstractState::AbstractState(uint8_t id, kickcat::AbstractESC2& esc)
        : id_{id}
        , esc_{esc}
    {
    }

    uint8_t AbstractState::id()
    {
        return id_;
    }

    void AbstractState::onEntry(uint8_t) {};

    void AbstractState::onExit(uint8_t) {};

    StateMachine::StateMachine(std::array<FSM::AbstractState*, 2>&& states)
        : states_{std::move(states)}
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
        currentState_->routine();
        uint8_t newStateId = currentState_->transition();

        if (newStateId != currentState_->id())
        {
            AbstractState* newState = getState(newStateId);
            if (not newState)
            {
                newState = states_[0];
            }

            if (newState != currentState_)
            {
                currentState_->onExit(newState->id());
                newState->onEntry(currentState_->id());
                currentState_ = newState;
            }
        }
    }
}
