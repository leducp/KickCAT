#include "Bus.h"
#include "kickcat/FoE/mailbox/request.h"

namespace kickcat
{
    using namespace mailbox::request;


    void Bus::readFoE(Slave& slave, std::string const& name, uint32_t password, std::vector<uint8_t>& file,
                      nanoseconds timeout, FoEProgress const& progress)
    {
        auto foe = slave.mailbox.createFoERead(name, password, timeout);
        waitForMessage(foe, [&]() { progress(foe->bytesTransferred()); });
        if (foe->status() != MessageStatus::SUCCESS)
        {
            THROW_ERROR_CODE("Error while reading FoE file", error::category::FoE, foe->status());
        }
        file = std::move(foe->file());
    }


    void Bus::writeFoE(Slave& slave, std::string const& name, uint32_t password, std::vector<uint8_t> file,
                       nanoseconds timeout, FoEProgress const& progress)
    {
        auto foe = slave.mailbox.createFoEWrite(name, password, std::move(file), timeout);
        waitForMessage(foe, [&]() { progress(foe->bytesTransferred()); });
        if (foe->status() != MessageStatus::SUCCESS)
        {
            THROW_ERROR_CODE("Error while writing FoE file", error::category::FoE, foe->status());
        }
    }


    void Bus::enterBootstrap(Slave& slave, nanoseconds timeout)
    {
        if ((slave.sii.info.bootstrap_recv_mbx_size == 0) or (slave.sii.info.bootstrap_send_mbx_size == 0))
        {
            THROW_ERROR("The slave SII does not declare a bootstrap mailbox");
        }

        requestState(slave, State::INIT);
        waitForState(slave, State::INIT, timeout);

        try
        {
            switchMailboxLayout(slave, true);
            requestState(slave, State::BOOT);
            waitForState(slave, State::BOOT, timeout);
        }
        catch (...)
        {
            // The slave may have reached BOOT unseen (lost status read, timeout): its SyncManagers are rewritten
            // only once it is confirmed back in INIT. The original error is the one reported.
            bool in_init = false;
            try
            {
                requestState(slave, State::INIT);
                waitForState(slave, State::INIT, timeout);
                in_init = true;
            }
            catch (...)
            {
            }
            if (in_init)
            {
                switchMailboxLayout(slave, false);
            }
            throw;
        }
    }


    void Bus::exitBootstrap(Slave& slave, nanoseconds timeout)
    {
        requestState(slave, State::INIT);
        waitForState(slave, State::INIT, timeout);

        switchMailboxLayout(slave, false);

        requestState(slave, State::PRE_OP);
        waitForState(slave, State::PRE_OP, timeout);
    }


    void Bus::switchMailboxLayout(Slave& slave, bool bootstrap)
    {
        // Pending messages belong to the previous layout: an unfinished transfer left in to_process would take the
        // replies of the next one. The handlers are rebuilt for the new mailbox size.
        slave.mailbox.to_send = {};
        slave.mailbox.to_process.clear();
        slave.mailbox.can_read  = false;
        slave.mailbox.can_write = false;

        slave.selectMailboxLayout(bootstrap);
        addMailboxHandlers(slave);
        addMailboxConfiguration(slave);
        link_->processDatagrams();
    }
}
