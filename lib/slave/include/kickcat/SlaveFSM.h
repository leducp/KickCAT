#ifndef SLAVE_STACK_INCLUDE_SLAVE_FSM_H_
#define SLAVE_STACK_INCLUDE_SLAVE_FSM_H_

#include <cstdarg>
#include <cstdint>
#include <map>
#include <string>
#include "PDO.h"
#include "kickcat/AbstractESC.h"
#include "kickcat/FSM.h"
#include "kickcat/Mailbox.h"
#include "kickcat/protocol.h"

// TODO: to rename file
namespace kickcat
{
    namespace FSM
    {
        class Init : public AbstractState
        {
        public:
            Init(AbstractESC& esc, PDO& pdo);

            void onEntry(uint8_t fromState) override;
            ALStatus routineInternal(ALStatus currentStatus, ALControl control) override;
        };

        class PreOP : public AbstractState
        {
        public:
            PreOP(AbstractESC& esc, PDO& pdo);

            void onEntry(uint8_t fromState) override;
            ALStatus routineInternal(ALStatus currentStatus, ALControl control) override;
        };

        class SafeOP : public AbstractState
        {
        public:
            SafeOP(AbstractESC& esc, PDO& pdo);

            void onEntry(uint8_t fromState) override;
            ALStatus routineInternal(ALStatus currentStatus, ALControl control) override;
        };

        class OP : public AbstractState
        {
        public:
            OP(AbstractESC& esc, PDO& pdo);

            ALStatus routineInternal(ALStatus currentStatus, ALControl control) override;

        private:
            bool has_expired_watchdog();
        };


    }
}
#endif
