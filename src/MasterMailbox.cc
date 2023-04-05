#include "MasterMailbox.h"

namespace kickcat
{
    MasterMailbox::MasterMailbox(CoE::MasterDeviceDescription& master_description)
    : master_description_(master_description)
    {
        printf("Master description test %i \n", master_description_.identity.number_of_entries);
    }


    std::shared_ptr<GatewayMessage> MasterMailbox::createProcessedGatewayMessage(uint8_t const* raw_message, int32_t raw_message_size, uint16_t gateway_index)
    {
        auto msg = std::make_shared<GatewayMessage>(raw_message_size, raw_message, gateway_index, 0ns);

        //Fill message.

        mailbox::Header const* header = reinterpret_cast<mailbox::Header const*>(msg->data());
        mailbox::ServiceData const* coe = reinterpret_cast<mailbox::ServiceData const*>(msg->data() + sizeof(mailbox::Header));

        std::vector<uint8_t> response; //contains mailbox header + serviceData + data
        response.resize(msg->size());

        if (header->type == mailbox::Type::CoE)
        {
            DEBUG_PRINT("VALID message type \n");
        }

        switch (coe->command)
        {
        case CoE::SDO::request::UPLOAD:
            response = replyUploadSDO(header->address, coe->index, coe->subindex, coe->complete_access);
            break;
        case CoE::SDO::request::DOWNLOAD:
        case CoE::SDO::request::DOWNLOAD_SEGMENTED:
        case CoE::SDO::request::UPLOAD_SEGMENTED:
        default:
            uint32_t abort_code = 0x06010000; //unsupported access
            response = createAbortSDO(header->address, coe->index, coe->subindex, abort_code);
        }


        uint32_t debug;
        memcpy(&debug, response.data() + sizeof(mailbox::Header) + sizeof(mailbox::ServiceData), 4);
        printf("debug %u \n", debug);
        printf("Header len %u \n", header->len);

        msg->process(response.data());
        return msg;
    }


    std::vector<uint8_t> MasterMailbox::createAbortSDO(uint16_t address, uint16_t index, uint8_t subindex, uint32_t abort_code)
    {
        // ETG1000_6 Table 40 – Abort SDO Transfer Request
        SDOFrame sdo(sizeof(abort_code));

        sdo.header_->len = sizeof(mailbox::ServiceData) + sizeof(abort_code);
        sdo.header_->address = address;
        sdo.header_->channel = 0; // unused;
        sdo.header_->priority = 0; // unused;
        sdo.header_->type = mailbox::CoE;
        sdo.header_->count = 0; // unused;

        sdo.coe_->number = 0;
        sdo.coe_->reserved = 0;
        sdo.coe_->service = CoE::Service::SDO_REQUEST;

        sdo.coe_->size_indicator = 0;
        sdo.coe_->transfer_type  = 0;
        sdo.coe_->block_size     = 0;
        sdo.coe_->complete_access = 0;
        sdo.coe_->command = CoE::SDO::request::ABORT;

        sdo.coe_->index = index;
        sdo.coe_->subindex = subindex;

        memcpy(sdo.payload_, &abort_code, sizeof(abort_code));

        return sdo.data_;
    }


    std::vector<uint8_t> MasterMailbox::replyUploadSDO(uint16_t address, uint16_t index, uint8_t subindex, bool complete_access)
    {
        // TODO no check in create, make them before hand ?
        // TODO handle complete access

        uint32_t size = 4;
        SDOFrame sdo(size);

        sdo.header_->len = sizeof(mailbox::ServiceData) + size;
        sdo.header_->address = address;
        sdo.header_->channel = 0; // unused;
        sdo.header_->priority = 0; // unused;
        sdo.header_->type = mailbox::CoE;
        sdo.header_->count = 0; // unused;

        sdo.coe_->number = 0;
        sdo.coe_->reserved = 0;
        sdo.coe_->service = CoE::Service::SDO_RESPONSE;

        sdo.coe_->size_indicator = 1;
        sdo.coe_->transfer_type  = 0;
        sdo.coe_->block_size     = 0;
        sdo.coe_->complete_access = uint8_t (complete_access);
        sdo.coe_->command = CoE::SDO::response::UPLOAD;

        sdo.coe_->index = index;
        sdo.coe_->subindex = subindex;

        int32_t payload = 42;
        memcpy(sdo.payload_, &payload, size);

        return sdo.data_;
    }
}
