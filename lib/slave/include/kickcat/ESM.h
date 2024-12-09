#ifndef SLAVE_STACK_INCLUDE_ESM_H_
#define SLAVE_STACK_INCLUDE_ESM_H_

#include <cstdarg>
#include "PDO.h"
#include "kickcat/AbstractESC.h"
#include "kickcat/protocol.h"


namespace kickcat
{
    constexpr uint8_t NUMBER_OF_STATES = 4;

    // TODO: to rename ESM
    namespace ESM
    {
        class StateMachine;

        struct ALControl
        {
            uint16_t value;

            State getRequestedState()
            {
                return static_cast<State>(value & State::MASK_STATE);
            }
        };

        struct ALStatus
        {
            uint16_t al_status;
            uint16_t al_status_code;
            uint16_t al_watchdog;
            bool validOutputData{false};

            State getState()
            {
                return static_cast<State>(al_status & State::MASK_STATE);
            }

            bool has_expired_watchdog()
            {
                return not(al_watchdog & 0x1);
            }

            static ALStatus build(uint8_t state, uint8_t statusCode = StatusCode::NO_ERROR)
            {
                if (statusCode == NO_ERROR)
                {
                    return ALStatus{state, statusCode, 0};
                }
                else
                {
                    return ALStatus{static_cast<uint16_t>(state | State::ERROR_ACK), statusCode, 0};
                }
            }
        };

        class AbstractState

        {
            friend StateMachine;

        public:
            AbstractState(uint8_t id, AbstractESC& esc, PDO& pdo);
            void setMailbox(mailbox::response::Mailbox* mbx);

        protected:
            uint8_t id_;
            AbstractESC& esc_;
            PDO& pdo_;
            mailbox::response::Mailbox* mbx_;

            virtual ALStatus routine(ALStatus currentStatus,
                                     ALControl alControl);  //return al_status and al_status_code

            virtual ALStatus routineInternal(ALStatus currentStatus,
                                             ALControl alControl) = 0;  //return al_status and al_status_code

            virtual void onEntry(uint8_t fromState)
            {
                (void)fromState;
            };

        private:
            uint8_t id();
        };

        class StateMachine final
        {
        public:
            StateMachine(AbstractESC& esc, std::array<ESM::AbstractState*, NUMBER_OF_STATES>&& states);
            void setOutputDataValid(bool isValid);
            void start();
            void play();
            State getState();

        private:
            AbstractState* findState(uint8_t id);

            AbstractESC& esc_;
            AbstractState* currentState_;
            std::array<ESM::AbstractState*, NUMBER_OF_STATES> states_;
            ALStatus status_{};
        };
    }
}
#endif
