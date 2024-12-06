#ifndef SLAVE_STACK_INCLUDE_FSM_H_
#define SLAVE_STACK_INCLUDE_FSM_H_

#include <cstdarg>
#include <map>
#include <string>
#include "kickcat/AbstractESC.h"
#include "kickcat/protocol.h"


namespace kickcat
{
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
            virtual std ::tuple<uint16_t, uint16_t> routine(
                uint16_t al_control,
                uint16_t al_status,
                uint16_t al_status_code) = 0;  //return al_status and al_status_code
            virtual void onEntry(uint8_t fromState);
        };

        class StateMachine
        {
        public:
            StateMachine(AbstractESC& esc, std::array<FSM::AbstractState*, 4>&& states);
            void start();
            void play();

        protected:
            AbstractState* getState(uint8_t id);

        private:
            AbstractESC& esc_;
            AbstractState* currentState_;
            std::array<FSM::AbstractState*, 4> states_;
            uint16_t al_status_{State::INIT};
            uint16_t al_status_code_{NO_ERROR};
        };
    }
}
#endif
