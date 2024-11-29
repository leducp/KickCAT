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
            AbstractState(uint8_t id, AbstractESC2& esc);

            uint8_t id();

        private:
            virtual void routine()       = 0;
            virtual uint8_t transition() = 0;
            virtual void onEntry(uint8_t oldState);
            virtual void onExit(uint8_t newState);

            uint8_t id_;

        protected:
            AbstractESC2& esc_;
        };

        class StateMachine
        {
        public:
            StateMachine(std::array<FSM::AbstractState*, 2>&& states);
            void init();
            void play();

        private:
            AbstractState* currentState_;
            std::array<FSM::AbstractState*, 2> states_;

            AbstractState* getState(uint8_t id);
        };
    }
}
#endif
