#include <cstring>

#include "MasterMailbox.h"

namespace kickcat
{
    std::shared_ptr<GatewayMessage> MasterMailbox::createGatewayMessage(uint8_t const* raw_message, int32_t raw_message_size, uint16_t gateway_index)
    {
        if (raw_message_size > recv_size)
        {
            DEBUG_PRINT("Message size is bigger than mailbox size\n");
            return nullptr;
        }

        auto msg = std::make_shared<GatewayMessage>(recv_size, raw_message, gateway_index, 0ns);

        //Fill message.

        mailbox::Header const* header = reinterpret_cast<mailbox::Header const*>(msg->data());

        if (header->type == mailbox::Type::CoE)
        {
            DEBUG_PRINT("VALID message type");
        }
        else
        {
            // ABORT
        }

        std::vector<uint8_t> response;
        response.resize(msg->size());
        std::memcpy(response.data(), msg->data(), msg->size());

        // Fail response in any case.
        mailbox::ServiceData* coe = reinterpret_cast<mailbox::ServiceData*>(response.data() + sizeof(mailbox::Header));
        // todo check coe->service == SDO_REQUEST otherwise ABORT.
        coe->service = CoE::Service::SDO_RESPONSE;
        coe->command = CoE::SDO::request::ABORT;

        msg->process(response.data());
        return msg;
    }

}
