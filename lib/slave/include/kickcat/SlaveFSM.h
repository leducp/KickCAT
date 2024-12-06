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

        protected:
            void set_al_status(State state);
            State getRequestedState(uint16_t al_control);
            std::tuple<uint16_t, uint16_t> buildALStatus(uint8_t state, uint8_t statusCode);
            void clear_error();


            AbstractESC& esc_;
            PDO& pdo_;
            mailbox::response::Mailbox* mbx_;
        };

        class Init : public SlaveState
        {
        public:
            Init(AbstractESC& esc, PDO& pdo);

            void onEntry(uint8_t fromState) override;
            std::tuple<uint16_t, uint16_t> routine(uint16_t al_control,
                                                   uint16_t al_status,
                                                   uint16_t al_status_code) override;
        };


        class PreOP : public SlaveState
        {
        public:
            PreOP(AbstractESC& esc, PDO& pdo);

            void onEntry(uint8_t fromState) override;
            std::tuple<uint16_t, uint16_t> routine(uint16_t al_control,
                                                   uint16_t al_status,
                                                   uint16_t al_status_code) override;
        };

        class SafeOP : public SlaveState
        {
        public:
            SafeOP(AbstractESC& esc, PDO& pdo);

            void onEntry(uint8_t fromState) override;
            std::tuple<uint16_t, uint16_t> routine(uint16_t al_control,
                                                   uint16_t al_status,
                                                   uint16_t al_status_code) override;
        };

        class OP : public SlaveState
        {
        public:
            OP(AbstractESC& esc, PDO& pdo);

            std::tuple<uint16_t, uint16_t> routine(uint16_t al_control,
                                                   uint16_t al_status,
                                                   uint16_t al_status_code) override;

        private:
            bool has_expired_watchdog();
        };


    }
}
#endif
