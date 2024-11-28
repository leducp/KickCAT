#ifndef SLAVE_STACK_INCLUDE_FSM_H_
#define SLAVE_STACK_INCLUDE_FSM_H_

#include <cstdarg>
#include <functional>
#include <string>


namespace kickcat
{
    namespace FSM
    {
        class State final
        {
        public:
            State(std::string const& name);

            void configure(
                std::function<void(State&)> routine,
                std::function<State*(State&)> guard,
                std::function<void(State&, State&)> onEntry = [](State&, State&) {},
                std::function<void(State&, State&)> onExit  = [](State&, State&) {});

            void routine();
            State* guard();
            void onEntry(State& oldState);
            void onExit(State& newState);
            std::string const& name();

            void log(char const* format, ...);

        private:
            std::function<void(State&)> routine_;
            std::function<State*(State&)> guard_;
            std::function<void(State&, State&)> onEntry_;
            std::function<void(State&, State&)> onExit_;

            std::string name_;
        };
        class StateMachine
        {
        public:
            StateMachine() = default;
            void init(State* initState);
            void routine();

        private:
            State* currentState_;
        };
    }
}
#endif
