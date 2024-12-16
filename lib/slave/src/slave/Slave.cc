#include "kickcat/slave/Slave.h"

namespace kickcat::slave
{
    Slave::Slave(AbstractESC* esc, PDO* pdo)
        : esc_{esc}
        , pdo_{pdo}
    {
    }

    void Slave::set_mailbox(mailbox::response::Mailbox* mbx)
    {
        mbx_ = mbx;
        init_.set_mailbox(mbx);
        preOp_.set_mailbox(mbx);
        safeOP_.set_mailbox(mbx);
        OP_.set_mailbox(mbx);
    }

    void Slave::start()
    {
        stateMachine_.start();
    }

    void Slave::routine()
    {
        if (mbx_)
        {
            mbx_->receive();
            mbx_->process();
            mbx_->send();
        }

        stateMachine_.play();
    }

    State Slave::getState()
    {
        return stateMachine_.get_state();
    }

    void Slave::validateOutputData()
    {
        stateMachine_.validateOutputData();
    }
}
