#ifndef SLAVE_STACK_INCLUDE_SLAVE_H_
#define SLAVE_STACK_INCLUDE_SLAVE_H_


#include "PDO.h"
#include "kickcat/AbstractESC.h"
#include "kickcat/Mailbox.h"
#include "kickcat/SlaveFSM.h"


// TODO: to rename file
namespace kickcat
{
    class Slave final
    {
    public:
        Slave(AbstractESC* esc, PDO* pdo);

        void set_mailbox(mailbox::response::Mailbox* mbx);
        void start();
        void routine();

    private:
        AbstractESC* esc_;
        mailbox::response::Mailbox* mbx_;
        PDO* pdo_;

        FSM::Init init_{*esc_, *pdo_};
        FSM::PreOP preOp_{*esc_, *pdo_};
        FSM::SafeOP safeOP_{*esc_, *pdo_};
        FSM::OP OP_{*esc_, *pdo_};
        FSM::StateMachine stateMachine{*esc_, {{&init_, &preOp_, &safeOP_, &OP_}}};
    };
}

#endif
