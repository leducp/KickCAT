#include "MasterMailbox.h"

namespace kickcat
{
    MasterMailbox::MasterMailbox()
    {
    }


    void MasterMailbox::init(CoE::MasterDeviceDescription& master_description)
    {
        objectDictionary_.insert({0x1000, {SDOData{&master_description.device_type, sizeof(master_description.device_type)}}});
        objectDictionary_.insert({0x1018, {
            // TODO macro create field
                                 SDOData{&master_description.identity.number_of_entries, sizeof(master_description.identity.number_of_entries)},
                                 SDOData{&master_description.identity.vendor_id, sizeof(master_description.identity.vendor_id)},
                                 SDOData{&master_description.identity.product_code, sizeof(master_description.identity.product_code)},
                                 SDOData{&master_description.identity.revision_number, sizeof(master_description.identity.revision_number)},
                                 SDOData{&master_description.identity.serial_number, sizeof(master_description.identity.serial_number)}
        }});
    }


    std::shared_ptr<GatewayMessage> MasterMailbox::answerGatewayMessage(uint8_t const* raw_message, int32_t raw_message_size, uint16_t gateway_index)
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
        auto const& entry =  objectDictionary_.find(index);

        if (entry == objectDictionary_.end())
        {
            uint32_t abort_code = 0x06020000; // object does not exists.
            return createAbortSDO(address, index, subindex, abort_code);
        }
        if (entry->second.size() < subindex)
        {
            uint32_t abort_code = 0x06090011; // subindex does not exists.
            return createAbortSDO(address, index, subindex, abort_code);
        }

        if (complete_access)
        {
            if (subindex > 1)
            {
                uint32_t abort_code = 0x06090011; // subindex does not exists.   // TODO check slave behavior, unsupported access ??
                return createAbortSDO(address, index, subindex, abort_code);
            }

            return createCompleteAccessUploadSDO(address, index, subindex);
        }

        SDOData& data = entry->second[subindex];
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

        return sdo.data_;
    }

    std::vector<uint8_t> MasterMailbox::createCompleteAccessUploadSDO(uint16_t address, uint16_t index, uint8_t subindex)
    {
        auto const& entry =  objectDictionary_.find(index);

        uint32_t size = 0;
        for (uint32_t i = subindex; i < entry->second.size(); i++)
        {
            size += entry->second[i].size;
        }

        SDOFrame sdo(size + 4);
        sdo.header_->len = sizeof(mailbox::ServiceData) + size + 4;
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
        sdo.coe_->complete_access = 1;
        sdo.coe_->command = CoE::SDO::response::UPLOAD;

        sdo.coe_->index = index;
        sdo.coe_->subindex = subindex;

        int32_t offset = 4; // complete size space
        for (uint32_t i = subindex; i < entry->second.size(); i++)
        {
            SDOData& data = entry->second[i];
            memcpy((uint8_t*)sdo.payload_ + offset, data.payload, data.size); // data
            offset += data.size;
            printf("i: %u, size: %i offset %i\n",i, data.size, offset);
        }
        memcpy(sdo.payload_, &size, 4); // fill complete size

        printf("complete access size %i sdo size %i \n", size, sdo.data_.size());

        uint32_t debug;
        memcpy(&debug, (uint8_t*)sdo.payload_ + 5, 4);
        printf("DEbug %i \n", debug);

        return sdo.data_;
    }
}
