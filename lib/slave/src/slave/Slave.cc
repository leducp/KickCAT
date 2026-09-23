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

    void Slave::setBootstrapMailbox(mailbox::response::Mailbox* mbx)
    {
        boot_mbx_ = mbx;
        init_.setBootstrapMailbox(mbx);
        boot_.setBootstrapMailbox(mbx);
    }

    void Slave::setDictionary(CoE::Dictionary* dictionary)
    {
        dictionary_ = dictionary;
        init_.setDictionary(dictionary);
        preOp_.setDictionary(dictionary);
        safeOP_.setDictionary(dictionary);
        OP_.setDictionary(dictionary);
        boot_.setDictionary(dictionary);
    }

    void Slave::start()
    {
        stateMachine_.start();
    }

    void Slave::routine()
    {
        mailbox::response::Mailbox* mbx = mbx_;
        if (stateMachine_.state() == State::BOOT)
        {
            mbx = boot_mbx_;
        }

        if (mbx)
        {
            mbx->receive();
            mbx->process();
            mbx->send();
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
