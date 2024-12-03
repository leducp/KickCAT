#ifndef SLAVE_STACK_INCLUDE_FSM_H_
#define SLAVE_STACK_INCLUDE_FSM_H_

#include <cstdarg>
#include <map>
#include <string>
#include "kickcat/protocol.h"


namespace kickcat
{
    class AbstractESC;

    namespace FSM
    {
        class StateMachine;

        class AbstractState
        {
            friend StateMachine;

        public:
            AbstractState(uint8_t id);
            virtual ~AbstractState() = default;

            uint8_t id();

        protected:
            uint8_t id_;

        private:
            virtual void startRoutine()  = 0;
            virtual void endRoutine()    = 0;
            virtual uint8_t transition() = 0;
            virtual void onEntry(uint8_t oldState);
            virtual void onExit(uint8_t newState);
        };

        class StateMachine
        {
        public:
            StateMachine(std::array<FSM::AbstractState*, 4>&& states);
            void start();
            void play();

        protected:
            AbstractState* getState(uint8_t id);

        private:
            AbstractState* currentState_;
            std::array<FSM::AbstractState*, 4> states_;
        };
    }
}
#endif
