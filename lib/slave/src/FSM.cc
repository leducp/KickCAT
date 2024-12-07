#include "kickcat/FSM.h"
#include "AbstractESC2.h"

namespace kickcat::FSM
{
    AbstractState::AbstractState(kickcat::AbstractESC2& esc, std::string const& name)
        : name_{name}
        , esc_{esc}
    {
    }

    std::string const& AbstractState::name()
    {
        return name_;
    }

    void AbstractState::onEntry(uint8_t) {};

    void AbstractState::onExit(uint8_t) {};

    StateMachine::StateMachine(std::map<uint8_t, FSM::AbstractState*>& states, kickcat::State defaultState)
        : states_{states}
    {
        currentState_ = defaultState;
        defaultState_ = defaultState;
    }

    void StateMachine::init()
    {
        states_[currentState_]->onEntry(currentState_);
    }

    void StateMachine::play()
    {
        states_[currentState_]->routine();
        uint8_t newState = states_[currentState_]->transition();

        if (newState != currentState_)
        {
            if (states_.find(newState) == states_.end())
            {
                newState = defaultState_;
            }

            if (newState != currentState_)
            {
                states_[currentState_]->onExit(newState);
                states_[newState]->onEntry(currentState_);
                currentState_ = newState;
            }
        }
    }
}
