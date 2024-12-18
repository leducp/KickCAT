#ifndef SLAVE_STACK_INCLUDE_SLAVE_ESM_H_
#define SLAVE_STACK_INCLUDE_SLAVE_ESM_H_

#include "PDO.h"
#include "kickcat/AbstractESC.h"
#include "kickcat/ESM.h"
#include "kickcat/Mailbox.h"

namespace kickcat
{
    namespace ESM
    {
        class Init final : public AbstractState
        {
        public:
            Init(AbstractESC& esc, PDO& pdo);
            virtual ~Init() = default;

            void onEntry(Context oldStatus, Context newStatus) override;
            Context routineInternal(Context oldStatus, ALControl control) override;
        };

        class PreOP final : public AbstractState
        {
        public:
            PreOP(AbstractESC& esc, PDO& pdo);
            virtual ~PreOP() = default;

            void onEntry(Context oldStatus, Context newStatus) override;
            Context routineInternal(Context oldStatus, ALControl control) override;
        };

        class SafeOP final : public AbstractState
        {
        public:
            SafeOP(AbstractESC& esc, PDO& pdo);
            virtual ~SafeOP() = default;

            void onEntry(Context oldStatus, Context newStatus) override;
            Context routineInternal(Context oldStatus, ALControl control) override;
        };

        class OP final : public AbstractState
        {
        public:
            OP(AbstractESC& esc, PDO& pdo);
            virtual ~OP() = default;

            Context routineInternal(Context oldStatus, ALControl control) override;

        private:
            bool has_expired_watchdog();
        };
    }
}
#endif
