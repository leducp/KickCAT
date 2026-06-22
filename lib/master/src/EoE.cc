#include "Bus.h"

namespace kickcat
{
    using namespace mailbox::request;

    void Bus::setIpParameter(Slave& slave, EoE::IpParameters const& params, nanoseconds timeout)
    {
        auto msg = slave.mailbox.createEoESetIP(params, timeout);
        waitForMessage(msg);
        if (msg->status() != MessageStatus::SUCCESS)
        {
            THROW_ERROR_CODE("Error while setting EoE IP parameter", error::category::EoE, msg->status());
        }
    }


    void Bus::getIpParameter(Slave& slave, EoE::IpParameters& params, nanoseconds timeout)
    {
        auto msg = slave.mailbox.createEoEGetIP(&params, timeout);
        waitForMessage(msg);
        if (msg->status() != MessageStatus::SUCCESS)
        {
            THROW_ERROR_CODE("Error while getting EoE IP parameter", error::category::EoE, msg->status());
        }
    }


    void Bus::setAddressFilter(Slave& slave, EoE::AddressFilter const& filter, nanoseconds timeout)
    {
        auto msg = slave.mailbox.createEoESetAddressFilter(filter, timeout);
        waitForMessage(msg);
        if (msg->status() != MessageStatus::SUCCESS)
        {
            THROW_ERROR_CODE("Error while setting EoE address filter", error::category::EoE, msg->status());
        }
    }


    void Bus::sendEoEFrame(Slave& slave, uint8_t const* frame, size_t len, uint8_t port)
    {
        slave.mailbox.sendEoEFrame(frame, len, port);
    }


    void Bus::setEoEFrameHandler(Slave& slave, EoE::FrameSink sink)
    {
        if (slave.eoe_receiver == nullptr)
        {
            THROW_ERROR("Slave has no EoE receiver (EoE not advertised in SII?)");
        }
        slave.eoe_receiver->setFrameSink(std::move(sink));
    }
}
