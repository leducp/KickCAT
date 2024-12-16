#include "kickcat/slave/Slave.h"

namespace kickcat::slave
{
    Slave::Slave(AbstractESC* esc, PDO* pdo)
        : esc_{esc}
        , pdo_{pdo}
    {
    }

    void Slave::setMailbox(mailbox::response::Mailbox* mbx)
    {
        mbx_ = mbx;
        init_.setMailbox(mbx);
        preOp_.setMailbox(mbx);
        safeOP_.setMailbox(mbx);
        OP_.setMailbox(mbx);
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

    State Slave::state()
    {
        return stateMachine_.state();
    }

    void Slave::validateOutputData()
    {
        stateMachine_.validateOutputData();
    }
}
