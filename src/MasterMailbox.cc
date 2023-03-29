#include <cstring>

#include "MasterMailbox.h"

namespace kickcat
{
    MasterMailbox::MasterMailbox(CoE::MasterDeviceDescription& master_description)
    : master_description_(master_description)
    {
        printf("Master description test %i \n", master_description_.identity.number_of_entries);
    }


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

        std::vector<uint8_t> response; //contains mailbox header + serviceData + data
        response.resize(msg->size());

        if (header->type == mailbox::Type::CoE)
        {
            DEBUG_PRINT("VALID message type \n");
        }

        // Default response: ABORT

        std::memcpy(response.data(), msg->data(), msg->size());
        // Fail response in any case.
        mailbox::ServiceData* coe = reinterpret_cast<mailbox::ServiceData*>(response.data() + sizeof(mailbox::Header));
        coe->service = CoE::Service::SDO_RESPONSE;
        coe->command = CoE::SDO::request::ABORT;


        // Test handle proper SDO response.

        //response = handleSDO(*msg);

        msg->process(response.data());
        return msg;
    }

    std::vector<uint8_t> MasterMailbox::handleSDO(GatewayMessage const& msg)
    {
        std::vector<uint8_t> response;
        response.resize(msg.size());
        std::memcpy(response.data(), msg.data(), msg.size());
        mailbox::ServiceData* coe = reinterpret_cast<mailbox::ServiceData*>(response.data() + sizeof(mailbox::Header));
        // todo check coe->service == SDO_REQUEST otherwise ABORT.
        coe->service = CoE::Service::SDO_RESPONSE;

        if (coe->command == CoE::SDO::request::UPLOAD)
        {
            uint8_t* payload = response.data() + sizeof(mailbox::Header) + sizeof(mailbox::ServiceData);

            printf("Ask for sdo upload index %x subindex %x \n", coe->index, coe->subindex);

            switch (coe->index)
            {
                case 0x1000:
                {
                    // todo check subindex
                    // todo check size ?

                    printf("Response device type %i \n", master_description_.device_type);
                    std::memcpy(payload, &master_description_.device_type, sizeof(master_description_.device_type));
                    break;
                }
                default:
                {
                    //abort
                }
            }
        }


        return response;
    }

}
