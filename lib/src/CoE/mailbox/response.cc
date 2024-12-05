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
        coe_->service = CoE::Service::SDO_REQUEST;
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
            abort(CoE::SDO::abort::READ_WRITE_ONLY_ACCESS);
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
                abort(CoE::SDO::abort::READ_WRITE_ONLY_ACCESS);
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
            abort(CoE::SDO::abort::WRITE_READ_ONLY_ACCESS);
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
                abort(CoE::SDO::abort::WRITE_READ_ONLY_ACCESS);
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
        switch (sdo_->opcode)
        {
            case CoE::SDO::information::GET_OD_LIST_REQ: { return processODList();  }
            case CoE::SDO::information::GET_OD_REQ:      { return processOD();      }
            case CoE::SDO::information::GET_ED_REQ:      { return processED();      }
            default:
            {
                abort(CoE::SDO::abort::COMMAND_SPECIFIER_INVALID);
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

        header_->len = sizeof(CoE::Header) + sizeof(CoE::ServiceDataInfo);
        sdo_->opcode = CoE::SDO::information::GET_OD_LIST_RESP;

        auto& dictionary = mailbox_->getDictionary();
        auto fillList = [&](ListType list_type, uint16_t access_check)
        {
            // 1. Compute answer
            std::vector<uint16_t> to_reply;
            to_reply.push_back(list_type);
            for (auto const& object : dictionary)
            {
                auto const& access = object.entries.at(0).access;
                if (access & access_check)
                {
                    to_reply.push_back(object.index);
                }
            }

            // 2. Compute required fragments
            std::size_t total_size = to_reply.size() * sizeof(uint16_t);
            uint16_t requiered_fragments = total_size / data_.size();
            if (total_size % data_.size())
            {
                requiered_fragments += 1;
            }

            // 3. Start replying the fragments
            std::size_t pos = 0;
            for (uint16_t fragment = 0; fragment < requiered_fragments; ++fragment)
            {
                std::vector<uint8_t> raw_reply = data_; // copy current message to save headers contexts

                auto header = pointData<mailbox::Header>(raw_reply.data());
                auto coe    = pointData<CoE::Header>(header);
                auto sdo    = pointData<CoE::ServiceDataInfo>(coe);
                auto data   = pointData<uint8_t>(sdo);

                // Update SDO info header
                sdo->fragments_left = requiered_fragments - fragment - 1;
                if (sdo->fragments_left)
                {
                    sdo->incomplete = 1;
                }

                while ((header->len + sizeof(uint16_t) <= (data_.size() - sizeof(mailbox::Header))) and (pos < to_reply.size()))
                {
                    std::memcpy(data, to_reply.data() + pos, sizeof(uint16_t));
                    pos += 1;
                    data += sizeof(uint16_t);
                    header->len += sizeof(uint16_t);
                }
                reply(std::move(raw_reply));

                // Update counter handle
                uint8_t counter = header_->count;
                header_->count = mailbox::nextCounter(counter);
            }
        };

        ListType list_type;
        std::memcpy(&list_type, sdo_ + 1, sizeof(ListType));
        switch (list_type)
        {
            case ListType::NUMBER:
            {
                uint16_t all_size      = static_cast<uint16_t>(dictionary.size());
                uint16_t rxpdo_size    = 0;
                uint16_t txpdo_size    = 0;
                uint16_t backup_size   = 0;
                uint16_t settings_size = 0;

                for (auto const& object : dictionary)
                {
                    auto const& access = object.entries.at(0).access;
                    if (access & CoE::Access::RxPDO)  { ++rxpdo_size;    }
                    if (access & CoE::Access::TxPDO)  { ++txpdo_size;    }
                    if (access & CoE::Access::BACKUP) { ++backup_size;   }
                    if (access & CoE::Access::SETTING){ ++settings_size; }
                }

                uint8_t* data = reinterpret_cast<uint8_t*>(sdo_ + 1) + 2;
                std::memcpy(data, &all_size,      sizeof(uint16_t)); data += sizeof(uint16_t);
                std::memcpy(data, &rxpdo_size,    sizeof(uint16_t)); data += sizeof(uint16_t);
                std::memcpy(data, &txpdo_size,    sizeof(uint16_t)); data += sizeof(uint16_t);
                std::memcpy(data, &backup_size,   sizeof(uint16_t)); data += sizeof(uint16_t);
                std::memcpy(data, &settings_size, sizeof(uint16_t)); data += sizeof(uint16_t);
                header_->len += sizeof(ListType) + sizeof(uint16_t) * 5;
                reply(std::move(data_));
                break;
            }
            case ListType::ALL:     { fillList(list_type, CoE::Access::ALL);       break; }
            case ListType::RxPDO:   { fillList(list_type, CoE::Access::RxPDO);     break; }
            case ListType::TxPDO:   { fillList(list_type, CoE::Access::TxPDO);     break; }
            case ListType::BACKUP:  { fillList(list_type, CoE::Access::BACKUP);    break; }
            case ListType::SETTINGS:{ fillList(list_type, CoE::Access::SETTING);   break; }
            default:
            {
                // TODO: unsupported access
            }
        }

        return ProcessingResult::FINALIZE;
    }

    ProcessingResult SDOInformationMessage::processOD()
    {
        header_->len = sizeof(CoE::Header) + sizeof(CoE::ServiceDataInfo);
        sdo_->opcode = CoE::SDO::information::GET_OD_RESP;

        auto desc = pointData<CoE::SDO::information::ObjectDescription>(sdo_);
        auto& dictionary = mailbox_->getDictionary();

        auto [object, entry] = findObject(dictionary, desc->index, 0);
        if ((object == nullptr) or (entry == nullptr))
        {
            abort(CoE::SDO::abort::OBJECT_DOES_NOT_EXIST);
            return ProcessingResult::FINALIZE;
        }

        desc->data_type    = entry->type;
        desc->max_subindex = object->entries.back().subindex;
        desc->object_code  = object->code;

        auto name = pointData<char>(desc);
        std::memcpy(name, object->name.data(), object->name.size());

        header_->len += sizeof(CoE::SDO::information::ObjectDescription) + object->name.size();

        reply(std::move(data_));
        return ProcessingResult::FINALIZE;
    }

    ProcessingResult SDOInformationMessage::processED()
    {
        header_->len = sizeof(CoE::Header) + sizeof(CoE::ServiceDataInfo);
        sdo_->opcode = CoE::SDO::information::GET_ED_RESP;

        auto desc = pointData<CoE::SDO::information::EntryDescription>(sdo_);
        auto& dictionary = mailbox_->getDictionary();

        auto [object, entry] = findObject(dictionary, desc->index, desc->subindex);
        if ((object == nullptr) or (entry == nullptr))
        {
            abort(CoE::SDO::abort::SUBINDEX_DOES_NOT_EXIST);
            return ProcessingResult::FINALIZE;
        }

        desc->value_info = 0; // TODO: we do not support any request yet, but it is still a valid answer
        desc->bit_length = entry->bitlen;
        desc->access     = entry->access;
        desc->data_type  = entry->type;

        auto name = pointData<char>(desc);
        std::memcpy(name, object->name.data(), object->name.size());

        header_->len += sizeof(CoE::SDO::information::EntryDescription) + object->name.size();

        reply(std::move(data_));
        return ProcessingResult::FINALIZE;
    }

    void SDOInformationMessage::abort(uint32_t code)
    {
        sdo_->opcode = CoE::SDO::information::SDO_INFO_ERROR_REQ;
        sdo_->incomplete = 0;
        sdo_->fragments_left = 0;

        auto payload = pointData<uint8_t>(sdo_);
        std::memcpy(payload, &code, sizeof(uint32_t));

        reply(std::move(data_));
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
