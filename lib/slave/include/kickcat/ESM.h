#ifndef SLAVE_STACK_INCLUDE_ESM_H_
#define SLAVE_STACK_INCLUDE_ESM_H_

#include "PDO.h"
#include "kickcat/AbstractESC.h"
#include "kickcat/protocol.h"


namespace kickcat
{
    constexpr uint8_t NUMBER_OF_STATES = 4;

    namespace ESM
    {
        class StateMachine;

        struct ALControl
        {
            uint16_t value;

            State requestedState()
            {
                return static_cast<State>(value & State::MASK_STATE);
            }
        };

        struct Context
        {
            uint16_t al_status;
            uint16_t al_status_code;
            uint16_t al_watchdog_process_data;
            bool is_valid_output_data{false};

            State state()
            {
                return static_cast<State>(al_status & State::MASK_STATE);
            }

            bool has_expired_watchdog()
            {
                return not(al_watchdog_process_data & 0x1);
            }

            static Context build(uint8_t state, uint8_t statusCode = StatusCode::NO_ERROR)
            {
                if (statusCode == NO_ERROR)
                {
                    return Context{state, statusCode, 0};
                }
                else
                {
                    return Context{static_cast<uint16_t>(state | State::ERROR_ACK), statusCode, 0};
                }
            }
        };

        class AbstractState
        {
            friend StateMachine;

        public:
            AbstractState(uint8_t id, AbstractESC& esc, PDO& pdo);
            void setMailbox(mailbox::response::Mailbox* mbx);
            virtual Context routine(Context currentStatus, ALControl alControl);

        protected:
            uint8_t id_;
            AbstractESC& esc_;
            PDO& pdo_;
            mailbox::response::Mailbox* mbx_{};

            virtual Context routineInternal(Context currentStatus, ALControl alControl) = 0;
            virtual void onEntry(Context oldStatus, Context newStatus) {};

            uint8_t id();
        };

        class StateMachine final
        {
        public:
            StateMachine(AbstractESC& esc, std::array<ESM::AbstractState*, NUMBER_OF_STATES>&& states);
            void validateOutputData();
            void start();
            void play();
            State state();

        private:
            AbstractState* find_state(uint8_t id);

            AbstractESC& esc_;
            AbstractState* current_state_;
            std::array<ESM::AbstractState*, NUMBER_OF_STATES> states_;
            Context status_{states_[0]->id(), 0, 0, false};
        };
    }
}
#endif
