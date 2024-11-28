#include "kickcat/FSM.h"
#include <iostream>
#include <ostream>

using namespace kickcat::FSM;

State::State(std::string const& name)
    : name_{name}
{
}

void State::configure(std::function<void(State&)> routine,
                      std::function<State*(State&)> guard,
                      std::function<void(State&, State&)> onEntry,
                      std::function<void(State&, State&)> onExit)
{
    routine_ = routine;
    guard_   = guard;
    onEntry_ = onEntry;
    onExit_  = onExit;
}

void State::routine()
{
    log("routine");
    routine_(*this);
};

State* State::guard()
{
    return guard_(*this);
}

void State::onEntry(State& oldState)
{
    log("Entry from %s", oldState.name().c_str());
    return onEntry_(oldState, *this);
}

void State::onExit(State& newState)
{
    log("Exiting... going to %s", newState.name().c_str());
    return onExit_(*this, newState);
}

std::string const& State::name()
{
    return name_;
}

void State::log(char const* format, ...)
{
    char output[1024];
    int size = sprintf(output, "%s : ", name_.c_str());
    std::va_list args;
    va_start(args, format);
    std::vsprintf(output + size, format, args);
    va_end(args);
    std::cout << output << std::endl;
}

void StateMachine::init(State* initState)
{
    currentState_ = initState;
    currentState_->onEntry(*currentState_);
}

void StateMachine::routine()
{
    currentState_->routine();
    auto* newState = currentState_->guard();
    if (newState != nullptr)
    {
        currentState_->onExit(*newState);
        newState->onEntry(*currentState_);
        currentState_ = newState;
    }
}

