#include <algorithm>
#include <cstring>

#include "Mailbox.h"
#include "kickcat/CoE/mailbox/response.h"

namespace kickcat::mailbox::response
{
    //using namespace kickcat::CoE;

    std::shared_ptr<AbstractMessage> createSDOMessage(Mailbox* mbx, std::vector<uint8_t>&& raw_message)
    {
        auto const* header = pointData<mailbox::Header>(raw_message.data());
        if (header->type != mailbox::Type::CoE)
        {
            return nullptr;
        }

        auto const* coe = pointData<CoE::Header>(header);
        if (coe->service != CoE::Service::SDO_REQUEST)
        {
            return nullptr;
        }

        return std::make_shared<SDOMessage>(mbx, std::move(raw_message), CoE::dictionary());
    }


    SDOMessage::SDOMessage(Mailbox* mbx, std::vector<uint8_t>&& raw_message, CoE::Dictionary& dictionary)
        : AbstractMessage{mbx}
        , dictionary_{dictionary}
    {
        data_ = std::move(raw_message);

        header_  = pointData<mailbox::Header>(data_.data());
        coe_     = pointData<CoE::Header>(header_);
        sdo_     = pointData<CoE::ServiceData>(coe_);
        payload_ = pointData<uint8_t>(sdo_);

    }

    void SDOMessage::abort(uint32_t code)
    {
        coe_->service = CoE::Service::SDO_RESPONSE;
        sdo_->command = CoE::SDO::request::ABORT;
        std::memcpy(payload_, &code, sizeof(uint32_t));

        reply(std::move(data_));
    }

    ProcessingResult SDOMessage::process()
    {
        auto [object, entry] = findObject(dictionary_, sdo_->index, sdo_->subindex);
        if (object == nullptr)
        {
            abort(CoE::SDO::abort::OBJECT_DOES_NOT_EXIST);
            return ProcessingResult::FINALIZE;
        }

        if (entry == nullptr)
        {
            abort(CoE::SDO::abort::SUBINDEX_DOES_NOT_EXIST);
            return ProcessingResult::FINALIZE;
        }

        if (sdo_->complete_access == 1)
        {
            if (sdo_->subindex > 1)
            {
                abort(CoE::SDO::abort::UNSUPPORTED_ACCESS);
                return ProcessingResult::FINALIZE;
            }

            switch (sdo_->command)
            {
                case CoE::SDO::request::UPLOAD:   { return uploadComplete  (object); }
                case CoE::SDO::request::DOWNLOAD: { return downloadComplete(object); }
            }
        }

        switch (sdo_->command)
        {
            case CoE::SDO::request::UPLOAD:   { return upload(entry);   }
            case CoE::SDO::request::DOWNLOAD: { return download(entry); }
        }

        return ProcessingResult::NOOP;
    }

    ProcessingResult SDOMessage::process(std::vector<uint8_t> const&)
    {
        return ProcessingResult::NOOP;
    }

    bool SDOMessage::isUploadAuthorized(CoE::Entry* entry)
    {
        //TODO: handle also other READ mode (depending on current state)
        return (entry->access & CoE::Access::READ);
    }

    bool SDOMessage::isDownloadAuthorized(CoE::Entry* entry)
    {
        return (entry->access & CoE::Access::WRITE);
    }

    ProcessingResult SDOMessage::upload(CoE::Entry* entry)
    {
        if (not isUploadAuthorized(entry))
        {
            abort(CoE::SDO::abort::WRITE_ONLY_ACCESS);
            return ProcessingResult::FINALIZE;
        }

        beforeHooks(CoE::Access::READ, entry);

        uint32_t size = entry->bitlen / 8;
        if (size <= 4)
        {
            // expedited
            sdo_->transfer_type = 1;
            sdo_->block_size = 4 - size;
        }
        else
        {
            sdo_->transfer_type = 0;
            std::memcpy(payload_, &size, 4);
            payload_ += 4;
        }

        std::memcpy(payload_, entry->data, size);

        header_->len  = sizeof(mailbox::Header) + sizeof(CoE::ServiceData) + size;
        coe_->service = CoE::Service::SDO_RESPONSE;
        sdo_->command = CoE::SDO::response::UPLOAD;
        reply(std::move(data_));

        afterHooks(CoE::Access::READ, entry);

        return ProcessingResult::FINALIZE;
    }

    ProcessingResult SDOMessage::uploadComplete(CoE::Object* object)
    {
        sdo_->transfer_type = 0; // complete access -> not expedited

        uint32_t size = 0;
        uint8_t number_of_entries = *(uint8_t*)object->entries.at(0).data;
        for (uint32_t i = sdo_->subindex; i <= number_of_entries; ++i)
        {
            auto* entry = &object->entries.at(i);
            if (not isUploadAuthorized(entry))
            {
                abort(CoE::SDO::abort::WRITE_ONLY_ACCESS);
                return ProcessingResult::FINALIZE;
            }

            beforeHooks(CoE::Access::READ, entry);

            uint32_t entry_size = entry->bitlen / 8;
            std::memcpy(payload_ + 4 + size, entry->data, entry_size);
            size += entry_size;

            afterHooks(CoE::Access::READ, entry);
        }

        std::memcpy(payload_, &size, 4);

        header_->len  = sizeof(mailbox::Header) + sizeof(CoE::ServiceData) + size;
        coe_->service = CoE::Service::SDO_RESPONSE;
        sdo_->command = CoE::SDO::response::UPLOAD_SEGMENTED;
        reply(std::move(data_));
        return ProcessingResult::FINALIZE;
    }

    ProcessingResult SDOMessage::download(CoE::Entry* entry)
    {
        if (not isDownloadAuthorized(entry))
        {
            abort(CoE::SDO::abort::READ_ONLY_ACCESS);
            return ProcessingResult::FINALIZE;
        }

        beforeHooks(CoE::Access::WRITE, entry);

        uint32_t size;
        if (sdo_->transfer_type)
        {
            size = 4 - sdo_->block_size;
        }
        else
        {
            std::memcpy(&size, payload_, 4);
            payload_ += 4;
        }

        if (size != (entry->bitlen / 8))
        {
            abort(CoE::SDO::abort::DATA_TYPE_LENGTH_MISMATCH);
            return ProcessingResult::FINALIZE;
        }

        std::memcpy(entry->data, payload_, size);

        coe_->service = CoE::Service::SDO_RESPONSE;
        sdo_->command = CoE::SDO::response::DOWNLOAD;
        reply(std::move(data_));

        afterHooks(CoE::Access::WRITE, entry);

        return ProcessingResult::FINALIZE;
    }

    ProcessingResult SDOMessage::downloadComplete(CoE::Object* object)
    {
        uint32_t msg_size;
        std::memcpy(&msg_size, payload_, 4);

        uint32_t size = 0;
        if (sdo_->subindex == 0)
        {
            auto* entry = &object->entries.at(0);
            beforeHooks(CoE::Access::WRITE, entry);

            std::memcpy(entry->data, payload_ + 4, 1);
            size += 2;

            afterHooks(CoE::Access::WRITE, entry);
        }

        uint16_t subindex = 1;
        while (size < msg_size)
        {
            if (subindex > object->entries.size())
            {
                abort(CoE::SDO::abort::SUBINDEX_DOES_NOT_EXIST);
                return ProcessingResult::FINALIZE;
            }

            auto* entry = &object->entries.at(subindex);
            if (not isDownloadAuthorized(entry))
            {
                abort(CoE::SDO::abort::READ_ONLY_ACCESS);
                return ProcessingResult::FINALIZE;
            }

            beforeHooks(CoE::Access::WRITE, entry);

            uint32_t entry_size = entry->bitlen / 8;
            std::memcpy(entry->data, payload_ + 4 + size, entry_size);
            size += entry_size;
            subindex++;

            afterHooks(CoE::Access::WRITE, entry);
        }

        coe_->service = CoE::Service::SDO_RESPONSE;
        sdo_->command = CoE::SDO::response::DOWNLOAD_SEGMENTED;
        reply(std::move(data_));
        return ProcessingResult::FINALIZE;
    }


    void SDOMessage::beforeHooks(uint16_t access, CoE::Entry* entry)
    {
        for (auto callback : entry->before_access)
        {
            callback(access, entry);
        }
    }


    void SDOMessage::afterHooks (uint16_t access, CoE::Entry* entry)
    {
        for (auto callback : entry->after_access)
        {
            callback(access, entry);
        }
    }
}
