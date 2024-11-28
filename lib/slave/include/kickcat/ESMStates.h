#ifndef SLAVE_STACK_INCLUDE_SLAVE_ESM_H_
#define SLAVE_STACK_INCLUDE_SLAVE_ESM_H_

#include <cstdarg>
#include <cstdint>
#include "PDO.h"
#include "kickcat/AbstractESC.h"
#include "kickcat/ESM.h"
#include "kickcat/Mailbox.h"

namespace kickcat
{
    namespace ESM
    {
        class Init : public AbstractState
        {
        public:
            Init(AbstractESC& esc, PDO& pdo);

            void on_entry(Context oldStatus, Context newStatus) override;
            Context routine_internal(Context oldStatus, ALControl control) override;
        };

        class PreOP : public AbstractState
        {
        public:
            PreOP(AbstractESC& esc, PDO& pdo);

            void on_entry(Context oldStatus, Context newStatus) override;
            Context routine_internal(Context oldStatus, ALControl control) override;
        };

        class SafeOP : public AbstractState
        {
        public:
            SafeOP(AbstractESC& esc, PDO& pdo);

            void on_entry(Context oldStatus, Context newStatus) override;
            Context routine_internal(Context oldStatus, ALControl control) override;
        };

        class OP : public AbstractState
        {
        public:
            OP(AbstractESC& esc, PDO& pdo);

            Context routine_internal(Context oldStatus, ALControl control) override;

        private:
            bool has_expired_watchdog();
        };
    }
}
#endif
