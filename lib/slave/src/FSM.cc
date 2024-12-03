#include "kickcat/FSM.h"
#include <algorithm>

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

    void AbstractState::onEntry() {};

    StateMachine::StateMachine(std::array<FSM::AbstractState*, 4>&& states)
        : states_{std::move(states)}
    {
        currentState_ = states_[0];
    }

    void StateMachine::start()
    {
        currentState_->onEntry();
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
        currentState_->startRoutine();
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
                newState->onEntry();
                currentState_ = newState;
            }
        }
        currentState_->endRoutine();
    }
}
