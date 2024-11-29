#ifndef SLAVE_STACK_INCLUDE_FSM_H_
#define SLAVE_STACK_INCLUDE_FSM_H_

#include <cstdarg>
#include <map>
#include <string>
#include "kickcat/protocol.h"


namespace kickcat
{
    class AbstractESC2;

    namespace FSM
    {
        class StateMachine;

        class AbstractState
        {
            friend StateMachine;

        public:
            AbstractState(AbstractESC2& esc, std::string const& name);

            std::string const& name();

        private:
            virtual void routine()       = 0;
            virtual uint8_t transition() = 0;
            virtual void onEntry(uint8_t oldState);
            virtual void onExit(uint8_t newState);

            std::string name_;

        protected:
            AbstractESC2& esc_;
        };

        class StateMachine
        {
        public:
            StateMachine(std::map<uint8_t, FSM::AbstractState*>& states, kickcat::State defaultState);
            void init();
            void play();

        private:
            uint8_t currentState_;
            uint8_t defaultState_;
            std::map<uint8_t, FSM::AbstractState*>& states_;
        };
    }
}
#endif
