#include <cstring>

#include "Mailbox.h"
#include "Error.h"

namespace kickcat
{
    uint8_t Mailbox::nextCounter()
    {
        // compute new counter - used as session handle
        counter++;
        if (counter > 7)
        {
            counter = 1;
        }
        return counter;
    }


    std::shared_ptr<AbstractMessage> Mailbox::createSDO(uint16_t index, uint8_t subindex, bool CA, uint8_t request, void* data, uint32_t* data_size)
    {
        if (recv_size == 0)
        {
            THROW_ERROR("This mailbox is inactive");
        }
        auto sdo = std::make_shared<SDOMessage>(recv_size, index, subindex, CA, request, data, data_size);
        sdo->setCounter(nextCounter());
        to_send.push(sdo);
        return sdo;
    }


    bool Mailbox::receive(uint8_t const* raw_message)
    {
        for (auto it = to_process.begin(); it != to_process.end(); ++it)
        {
            ProcessingResult state = (*it)->process(raw_message);
            switch (state)
            {
                case ProcessingResult::NOOP:
                {
                    continue;
                }
                case ProcessingResult::CONTINUE:
                {
                    (*it)->setCounter(nextCounter());
                    to_send.push(*it);
                    it = to_process.erase(it);
                    return true;
                }
                case ProcessingResult::FINALIZE:
                {
                    it = to_process.erase(it);
                    return true;
                }
            }
        }

        return false;
    }


    AbstractMessage::AbstractMessage(uint16_t mailbox_size)
    {
        data_.resize(mailbox_size);
        header_ = reinterpret_cast<mailbox::Header*>(data_.data());
        header_->address  = 0; // master
    }

    SDOMessage::SDOMessage(uint16_t mailbox_size, uint16_t index, uint8_t subindex, bool CA, uint8_t request, void* data, uint32_t* data_size)
        : AbstractMessage(mailbox_size)
        , client_data_(reinterpret_cast<uint8_t*>(data))
        , client_data_size_(data_size)
    {
        coe_ = reinterpret_cast<mailbox::ServiceData*>(data_.data() + sizeof(mailbox::Header));
        payload_ = reinterpret_cast<uint8_t*>(data_.data() + sizeof(mailbox::Header) + sizeof(mailbox::ServiceData));

        header_->len      = 10;
        header_->priority = 0; // unused
        header_->channel  = 0;
        header_->type     = mailbox::Type::CoE;

        coe_->number = 0;
        coe_->service = CoE::Service::SDO_REQUEST;
        coe_->complete_access = CA;
        coe_->command         = request;
        coe_->block_size      = 0;
        coe_->transfer_type   = 0;
        coe_->size_indicator  = 0;
        coe_->index    = index;
        coe_->subindex = subindex;

        if (request == CoE::SDO::request::DOWNLOAD)
        {
            uint32_t size = *data_size;
            if (size > (data_.size() - 10))
            {
                THROW_ERROR("Segmented download transfer required - not implemented");
            }

            if (size <= 4)
            {
                // expedited transfer
                coe_->transfer_type  = 1;
                coe_->size_indicator = 1;
                coe_->block_size = 4 - size;
                std::memcpy(payload_, data, size);
            }
            else
            {
                header_->len += size;
                std::memcpy(payload_, &size, sizeof(uint32_t));
                std::memcpy(payload_ + sizeof(uint32_t), data, size);
            }
        }

        status_ = MessageStatus::RUNNING;
    }


    ProcessingResult SDOMessage::process(uint8_t const* received)
    {
        mailbox::Header const* header = reinterpret_cast<mailbox::Header const*>(received);
        mailbox::ServiceData const* coe = reinterpret_cast<mailbox::ServiceData const*>(received + sizeof(mailbox::Header));
        uint8_t const* payload = received + sizeof(mailbox::Header) + sizeof(mailbox::ServiceData);

        // check if the received message is related to this one
        if (header->type != mailbox::Type::CoE)
        {
            return ProcessingResult::NOOP;
        }

        if ((coe->service != CoE::Service::SDO_REQUEST) and (coe->service != CoE::Service::SDO_RESPONSE))
        {
            return ProcessingResult::NOOP;
        }

        // check index and subindex for non segmented request
        if ((coe_->command == CoE::SDO::request::UPLOAD) or (coe_->command == CoE::SDO::request::DOWNLOAD))
        {
            if ((coe->index != coe_->index) or (coe->subindex != coe_->subindex))
            {
                return ProcessingResult::NOOP;
            }
        }

        // Message IS related: same index/subindex and right mailbox session handle -> low risk of collision
        // => check if message response is coherent
        if (coe->command == CoE::SDO::request::ABORT)
        {
            uint32_t code = *reinterpret_cast<uint32_t const*>(payload);
            // TODO: let client display itself the message
            std::string_view text = CoE::SDO::abort_to_str(code);
            printf("Abort requested! code %08x - %.*s\n", code, text.size(), text.data());
            status_ = code;
            return ProcessingResult::FINALIZE;
        }

        // everything is fine: process the payload
        switch (coe_->command)
        {
            case CoE::SDO::request::UPLOAD:               { return processUpload(header, coe, payload);            }
            case CoE::SDO::request::UPLOAD_SEGMENTED:     { return processUploadSegmented(header, coe, payload);   }
            case CoE::SDO::request::DOWNLOAD:             { return processDownload(header, coe, payload);          }
            case CoE::SDO::request::DOWNLOAD_SEGMENTED:   { return processDownloadSegmented(header, coe, payload); }
            default:
            {
                status_ = MessageStatus::COE_UNKNOWN_SERVICE;
                return ProcessingResult::FINALIZE;
            }
        }
    }


    ProcessingResult SDOMessage::processUpload(mailbox::Header const* header, mailbox::ServiceData const* coe, uint8_t const* payload)
    {
        if (coe->command != CoE::SDO::response::UPLOAD)
        {
            status_ = MessageStatus::COE_WRONG_SERVICE;
            return ProcessingResult::FINALIZE;
        }

        if (coe->transfer_type == 1)
        {
            // expedited transfer
            int32_t size = 4 - coe->block_size;
            if(*client_data_size_ < size)
            {
                status_ = MessageStatus::COE_CLIENT_BUFFER_TOO_SMALL;
                return ProcessingResult::FINALIZE;
            }
            std::memcpy(client_data_, payload, size);
            *client_data_size_ = size;

            status_ = MessageStatus::SUCCESS;
            return ProcessingResult::FINALIZE;
        }

        // standard or segmented transfer
        uint32_t complete_size = *reinterpret_cast<uint32_t const*>(payload);
        payload += 4;

        if (*client_data_size_ < complete_size)
        {
            status_ = MessageStatus::COE_CLIENT_BUFFER_TOO_SMALL;
            return ProcessingResult::FINALIZE;
        }

        if ((header->len - 10) >= complete_size)
        {
            // standard
            std::memcpy(client_data_, payload, complete_size);
            *client_data_size_ = complete_size;

            status_ = MessageStatus::SUCCESS;
            return ProcessingResult::FINALIZE;
        }

        // segmented
        uint32_t size = *reinterpret_cast<uint32_t const*>(payload);
        payload += 4;

        std::memcpy(client_data_, payload, size);
        client_data_ += size;
        *client_data_size_ = size;

        // since transfer is segmented, we need to request the next segment
        coe_->service = CoE::Service::SDO_REQUEST;
        coe_->command = CoE::SDO::request::UPLOAD_SEGMENTED;
        coe_->complete_access = false; // use for toggle bit - first segment shall be set to 0
        coe_->block_size      = 0;
        coe_->transfer_type   = 0;
        coe_->size_indicator  = 0;
        status_ = MessageStatus::RUNNING;
        return ProcessingResult::CONTINUE;
    }

    ProcessingResult SDOMessage::processUploadSegmented(mailbox::Header const* header, mailbox::ServiceData const* coe, uint8_t const* payload)
    {
        if (coe->command != CoE::SDO::response::UPLOAD_SEGMENTED)
        {
            status_ = MessageStatus::COE_WRONG_SERVICE;
            return ProcessingResult::FINALIZE;
        }

        if (coe->complete_access != coe_->complete_access)
        {
            status_ = MessageStatus::COE_SEGMENT_BAD_TOGGLE_BIT;
            return ProcessingResult::FINALIZE;
        }

        uint32_t size = 0;
        if (header->len == 10)
        {
            size = 7 - (coe->block_size | (coe->size_indicator << 2));
        }
        else
        {
            size = *reinterpret_cast<uint32_t const*>(payload);
            payload += sizeof(uint32_t);
        }

        std::memcpy(client_data_, payload, size);
        client_data_ += size;
        *client_data_size_ += size;

        bool more_follow = coe->size_indicator;
        if (not more_follow)
        {
            status_ = MessageStatus::SUCCESS;
            return ProcessingResult::FINALIZE;
        }

        coe_->complete_access = not coe_->complete_access;
        return ProcessingResult::CONTINUE;
    }

    ProcessingResult SDOMessage::processDownload(mailbox::Header const* header, mailbox::ServiceData const* coe, uint8_t const* payload)
    {
        if (coe->command != CoE::SDO::response::DOWNLOAD)
        {
            status_ = MessageStatus::COE_WRONG_SERVICE;
            return ProcessingResult::FINALIZE;
        }

        status_ = MessageStatus::SUCCESS; // all checks passed
        return ProcessingResult::FINALIZE;
    }

    ProcessingResult SDOMessage::processDownloadSegmented(mailbox::Header const* header, mailbox::ServiceData const* coe, uint8_t const* payload)
    {
        if (coe->command != CoE::SDO::response::DOWNLOAD_SEGMENTED)
        {
            status_ = MessageStatus::COE_WRONG_SERVICE;
            return ProcessingResult::FINALIZE;
        }

        return ProcessingResult::FINALIZE;
    }
}
