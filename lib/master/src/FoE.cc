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
}
