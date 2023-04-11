#include "MasterMailbox.h"

namespace kickcat
{
    MasterMailbox::MasterMailbox()
    {
    }


    void MasterMailbox::init(CoE::MasterDeviceDescription& master_description)
    {
        objectDictionary_.insert({0x1000, {{{0, SDOField{&master_description.device_type, sizeof(master_description.device_type)}}}, false, nullptr, 0}});
        objectDictionary_.insert({0x1018,
                                 {
                                     {
                                         {0, SDOField{&master_description.identity.number_of_entries, sizeof(master_description.identity.number_of_entries)}},
                                         {1, SDOField{&master_description.identity.vendor_id, sizeof(master_description.identity.vendor_id)}}
                                     }, true, &master_description.identity.number_of_entries, sizeof(CoE::IdentityObject)
                                 }
//                                 SDOField{&master_description.identity.product_code, sizeof(master_description.identity.product_code)},
//                                 SDOField{&master_description.identity.revision_number, sizeof(master_description.identity.revision_number)},
//                                 SDOField{&master_description.identity.serial_number, sizeof(master_description.identity.serial_number)}
        });
    }


    std::shared_ptr<GatewayMessage> MasterMailbox::replyGatewayMessage(uint8_t const* raw_message, int32_t raw_message_size, uint16_t gateway_index)
    {
        auto msg = std::make_shared<GatewayMessage>(raw_message_size, raw_message, gateway_index, 0ns);

        //Fill message.

        mailbox::Header const* header = reinterpret_cast<mailbox::Header const*>(msg->data());
        mailbox::ServiceData const* coe = reinterpret_cast<mailbox::ServiceData const*>(msg->data() + sizeof(mailbox::Header));

        std::vector<uint8_t> response; //contains mailbox header + serviceData + data

        if (header->type == mailbox::Type::CoE)
        {
            DEBUG_PRINT("VALID message type \n");
        }

        switch (coe->command)
        {
            case CoE::SDO::request::UPLOAD:
            {
                response = replyUploadSDO(header->address, coe->index, coe->subindex, coe->complete_access);
                break;
            }
            case CoE::SDO::request::DOWNLOAD:
            case CoE::SDO::request::DOWNLOAD_SEGMENTED:
            case CoE::SDO::request::UPLOAD_SEGMENTED:
            default:
            {
                uint32_t abort_code = 0x06010000; //unsupported access
                response = createAbortSDO(header->address, coe->index, coe->subindex, abort_code);
            }
        }

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
        auto const& entryIt =  objectDictionary_.find(index);
        SDOObject const& entry = entryIt->second;

        if (entryIt == objectDictionary_.end())
        {
            uint32_t abort_code = 0x06020000; // object does not exists.
            return createAbortSDO(address, index, subindex, abort_code);
        }
        if (entry.fields.size() < subindex)
        {
            uint32_t abort_code = 0x06090011; // subindex does not exists.
            return createAbortSDO(address, index, subindex, abort_code);
        }

        SDOField data;
        if (complete_access)
        {
            if (subindex > 1 or not entry.complete_access_enable)
            {
                uint32_t abort_code = 0x06010000; // Unsupported access to object
                return createAbortSDO(address, index, subindex, abort_code);
            }

            if (subindex == 1)
            {
                printf("SUBINDEX 1 \n");
                // skip subindex 0
                uint32_t sizeSubindex0 = entry.fields.at(0).size;
                data.payload = (uint8_t*)entry.payload_complete_access + sizeSubindex0;
                data.size = entry.size_complete_access - sizeSubindex0;
            }
            else
            {
                printf("SUBINDEX 0 \n");
                data.payload = entry.payload_complete_access;
                data.size = entry.size_complete_access;
            }
        }
        else
        {
            data = entry.fields.at(subindex);
        }

        SDOFrame sdo(data.size + 4);
        sdo.header_->len = sizeof(mailbox::ServiceData) + data.size + 4;
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
        memcpy(sdo.payload_, &data.size, 4); // fill complete size
        memcpy((uint8_t*)sdo.payload_ + 4, data.payload, data.size); // data

        CoE::IdentityObject debug{1,1,1,1,1};
        memcpy(&debug.vendor_id, data.payload, data.size);
        printf("Debug: %u %u %u %u \n", debug.vendor_id, debug.product_code, debug.revision_number, debug.serial_number);

        return sdo.data_;
    }
}
