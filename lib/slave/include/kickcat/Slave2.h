#ifndef SLAVE_STACK_INCLUDE_SLAVE_H_
#define SLAVE_STACK_INCLUDE_SLAVE_H_


#include "PDO.h"
#include "kickcat/AbstractESC.h"
#include "kickcat/Mailbox.h"
#include "kickcat/SlaveFSM.h"

namespace kickcat
{
    class Slave final
    {
    public:
        Slave(AbstractESC* esc, PDO* pdo)
            : esc_{esc}
            , pdo_{pdo}
        {
        }

        void set_mailbox(mailbox::response::Mailbox* mbx)
        {
            mbx_ = mbx;
            init_.setMailbox(mbx);
            preOp_.setMailbox(mbx);
            safeOP_.setMailbox(mbx);
            OP_.setMailbox(mbx);
        }

        void start()
        {
            stateMachine.start();
        }

        void routine()
        {
            mbx_->receive();
            mbx_->process();
            mbx_->send();

            stateMachine.play();
        }

    private:
        AbstractESC* esc_;
        mailbox::response::Mailbox* mbx_;
        PDO* pdo_;

        FSM::Init init_{*esc_, *pdo_};
        FSM::PreOP preOp_{*esc_, *pdo_};
        FSM::SafeOP safeOP_{*esc_, *pdo_};
        FSM::OP OP_{*esc_, *pdo_};
        FSM::StateMachine stateMachine{{&init_, &preOp_, &safeOP_, &OP_}};
    };
}

#endif
