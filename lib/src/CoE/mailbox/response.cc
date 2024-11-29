#include <cstdint>
#include <cstring>

#include "Mailbox.h"
#include "kickcat/CoE/mailbox/response.h"
#include "protocol.h"

namespace kickcat::mailbox::response
{
    std::shared_ptr<AbstractMessage> createSDOMessage(Mailbox* mbx, std::vector<uint8_t>&& raw_message)
    {
        auto const* header = pointData<mailbox::Header>(raw_message.data());
        if (header->type != mailbox::Type::CoE)
        {
            return nullptr;
        }

        auto const* coe = pointData<CoE::Header>(header);
        switch (coe->service)
        {
            case CoE::Service::SDO_REQUEST:
            {
                return std::make_shared<SDOMessage>(mbx, std::move(raw_message));
            }
            case CoE::Service::EMERGENCY:
            case CoE::Service::SDO_RESPONSE:
            case CoE::Service::TxPDO:
            case CoE::Service::RxPDO:
            case CoE::Service::TxPDO_REMOTE_REQUEST:
            case CoE::Service::RxPDO_REMOTE_REQUEST:
            {
                return nullptr;
            }
            case CoE::Service::SDO_INFORMATION:
            {
                return std::make_shared<SDOInformationMessage>(mbx, std::move(raw_message));
            }

            default:
            {
                return std::make_shared<MailboxErrorMessage>(
                    mbx, std::move(raw_message), mailbox::Error::INVALID_HEADER);
            }
        }
    }


    SDOMessage::SDOMessage(Mailbox* mbx, std::vector<uint8_t>&& raw_message)
        : AbstractMessage{mbx}
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
        if (header_->len < (sizeof(mailbox::Header) + sizeof(CoE::ServiceData)))
        {
            replyError(std::move(data_), mailbox::Error::SIZE_TOO_SHORT);
            return ProcessingResult::FINALIZE;
        }

        auto [object, entry] = findObject(mailbox_->getDictionary(), sdo_->index, sdo_->subindex);
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
        header_->len  = sizeof(mailbox::Header) + sizeof(CoE::ServiceData);

        if (size <= 4)
        {
            // expedited
            sdo_->transfer_type  = 1;
            sdo_->block_size     = 4 - size;
            sdo_->size_indicator = 1;
        }
        else
        {
            sdo_->transfer_type = 0;
            std::memcpy(payload_, &size, 4);
            payload_ += 4;
            header_->len += size;
            sdo_->size_indicator = 0;
        }

        std::memcpy(payload_, entry->data, size);

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

    void SDOMessage::afterHooks(uint16_t access, CoE::Entry* entry)
    {
        for (auto callback : entry->after_access)
        {
            callback(access, entry);
        }
    }


    SDOInformationMessage::SDOInformationMessage(Mailbox* mbx, std::vector<uint8_t>&& raw_message)
        : AbstractMessage{mbx}
    {
        data_ = std::move(raw_message);

        header_  = pointData<mailbox::Header>(data_.data());
        coe_     = pointData<CoE::Header>(header_);
        sdo_     = pointData<CoE::ServiceDataInfo>(coe_);
    }


    ProcessingResult SDOInformationMessage::process()
    {
        /*
        if (header_->len < (sizeof(mailbox::Header) + sizeof(CoE::ServiceDataInfo)))
        {
            replyError(std::move(data_), mailbox::Error::SIZE_TOO_SHORT);
            return ProcessingResult::FINALIZE;
        }
        */

        switch (sdo_->opcode)
        {
            case CoE::SDO::information::GET_OD_LIST_REQ: { return processODList();  }
            case CoE::SDO::information::GET_OD_REQ:      { return processOD();      }
            case CoE::SDO::information::GET_ED_REQ:      { return processED();      }
            default:
            {
                //abort(CoE::SDO::abort::UNSUPPORTED_ACCESS); //TODO abort
                return ProcessingResult::FINALIZE;
            }
        }
    }


    ProcessingResult SDOInformationMessage::process(std::vector<uint8_t> const&)
    {
        return ProcessingResult::NOOP;
    }


    ProcessingResult SDOInformationMessage::processODList()
    {
        using CoE::SDO::information::ListType;

        header_->len = sizeof(CoE::Header) + sizeof(CoE::ServiceDataInfo) + sizeof(ListType);

        ListType* list_type = pointData<ListType>(sdo_);
        uint16_t* data = pointData<uint16_t>(list_type);

        auto& dictionary = mailbox_->getDictionary();
        switch (*list_type)
        {
            case ListType::NUMBER:
            {
                data[0] = static_cast<uint16_t>(dictionary.size());
                uint16_t rxpdo_size    = 0;
                uint16_t txpdo_size    = 0;
                uint16_t backup_size   = 0;
                uint16_t settings_size = 0;

                for (auto const& object : dictionary)
                {
                    auto const& access = object.entries.at(0).access;
                    if (access & CoE::Access::RxPDO)
                    {
                        ++rxpdo_size;
                    }
                    if (access & CoE::Access::TxPDO)
                    {
                        ++txpdo_size;
                    }
                    if (access & CoE::Access::BACKUP)
                    {
                        ++backup_size;
                    }
                    if (access & CoE::Access::SETTING)
                    {
                        ++settings_size;
                    }
                }

                data[1] = rxpdo_size;
                data[2] = txpdo_size;
                data[3] = backup_size;
                data[4] = settings_size;
                header_->len += sizeof(uint16_t) * 5;
                printf("yay %d %x %x %x %x %x\n",
                    header_->len, data[0], data[1], data[2], data[3], data[4]);
                break;
            }
            case ListType::ALL:
            {
                printf("ALL ! %d\n", dictionary.size());
                for (auto const& object : dictionary)
                {
                    *data = object.index;
                    data++;
                }
                header_->len += 2 * dictionary.size();
                break;
            }
            case ListType::RxPDO:       { break; }
            case ListType::TxPDO:       { break; }
            case ListType::BACKUP:      { break; }
            case ListType::SETTINGS:    { break; }
        }

        sdo_->opcode = CoE::SDO::information::GET_OD_LIST_RESP;
        reply(std::move(data_));

        return ProcessingResult::FINALIZE;
    }

    ProcessingResult SDOInformationMessage::processOD()
    {
        return ProcessingResult::FINALIZE;
    }

    ProcessingResult SDOInformationMessage::processED()
    {
        return ProcessingResult::FINALIZE;
    }

    MailboxErrorMessage::MailboxErrorMessage(Mailbox* mbx, std::vector<uint8_t>&& raw_message, uint16_t error)
        : AbstractMessage{mbx}
        , error_{error}
    {
        data_ = std::move(raw_message);
    }

    ProcessingResult MailboxErrorMessage::process()
    {
        replyError(std::move(data_), mailbox::Error::INVALID_HEADER);
        return ProcessingResult::FINALIZE;
    }

    ProcessingResult MailboxErrorMessage::process(std::vector<uint8_t> const&)
    {
        return ProcessingResult::NOOP;
    }
}
