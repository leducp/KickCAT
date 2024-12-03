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


namespace kickcat
{
    namespace FSM
    {

        class SlaveState : public AbstractState
        {
        public:
            SlaveState(uint8_t id, AbstractESC& esc, PDO& pdo);
            void setMailbox(mailbox::response::Mailbox* mbx);

            void startRoutine();
            void endRoutine();
            void onEntry();

        protected:
            virtual void routine() {};
            virtual void onEntryInternal() {};

            void set_al_status(State state);
            State getRequestedState();
            void set_error(StatusCode code);
            void clear_error();

            StatusCode al_status_code_ = {StatusCode::NO_ERROR};
            uint16_t al_status_        = {0};
            uint16_t al_control_       = {0};

            AbstractESC& esc_;
            PDO& pdo_;
            mailbox::response::Mailbox* mbx_;
        };

        class Init : public SlaveState
        {
        public:
            Init(AbstractESC& esc, PDO& pdo);

            void onEntryInternal();
            uint8_t transition();
        };

        class PreOP : public SlaveState
        {
        public:
            PreOP(AbstractESC& esc, PDO& pdo);

            void onEntryInternal();
            uint8_t transition();
        };

        class SafeOP : public SlaveState
        {
        public:
            SafeOP(AbstractESC& esc, PDO& pdo);

            void onEntryInternal();
            void routine();
            uint8_t transition();
        };

        class OP : public SlaveState
        {
        public:
            OP(AbstractESC& esc, PDO& pdo);

            void routine();
            uint8_t transition();

        private:
            bool has_expired_watchdog();
        };


    }
}
#endif
