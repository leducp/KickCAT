#ifndef SLAVE_STACK_INCLUDE_SLAVE_FSM_H_
#define SLAVE_STACK_INCLUDE_SLAVE_FSM_H_

#include <cstdarg>
#include <cstdint>
#include <map>
#include <string>
#include "kickcat/FSM.h"
#include "kickcat/protocol.h"


namespace kickcat
{
    class AbstractESC2;

    namespace FSM
    {
        class Init : public AbstractState
        {
        public:
            Init(AbstractESC2& esc);

            void routine();
            uint8_t transition();
            void onEntry(uint8_t oldState);
            void onExit(uint8_t newState);
        };

        class PreOP : public AbstractState
        {
        public:
            PreOP(AbstractESC2& esc);

            void routine();
            uint8_t transition();
            void onEntry(uint8_t oldState);
            void onExit(uint8_t newState);
        };
    }
}
#endif
