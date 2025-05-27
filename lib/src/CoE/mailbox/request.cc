#include <cstring>
#include <cinttypes>

#include "debug.h"
#include "kickcat/CoE/mailbox/request.h"

namespace kickcat::mailbox::request
{
    SDOMessage::SDOMessage(uint16_t mailbox_size, uint16_t index, uint8_t subindex, bool CA, uint8_t request, void* data, uint32_t* data_size, nanoseconds timeout)
        : AbstractMessage(mailbox_size, timeout)
        , client_data_(reinterpret_cast<uint8_t*>(data))
        , client_data_size_(data_size)
    {
        coe_ = pointData<CoE::Header>(header_);
        sdo_ = pointData<CoE::ServiceData>(coe_);
        payload_ = pointData<uint8_t>(sdo_);

        header_->len      = 10;
        header_->priority = 0; // unused
        header_->channel  = 0;
        header_->type     = mailbox::Type::CoE;

        coe_->number = 0;
        coe_->service = CoE::Service::SDO_REQUEST;
        sdo_->complete_access = CA;
        sdo_->command         = request & 0x7;
        sdo_->block_size      = 0;
        sdo_->transfer_type   = 0;
        sdo_->size_indicator  = 0;
        sdo_->index    = index;
        sdo_->subindex = subindex;

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
                sdo_->transfer_type  = 1;
                sdo_->size_indicator = 1;
                sdo_->block_size = (4 - size) & 0x3;
                std::memcpy(payload_, data, size);
            }
            else
            {
                header_->len += static_cast<uint16_t>(size);
                std::memcpy(payload_, &size, sizeof(uint32_t));
                std::memcpy(payload_ + sizeof(uint32_t), data, size);
            }
        }
    }


    ProcessingResult SDOMessage::process(uint8_t const* received)
    {
        auto const* header  = pointData<mailbox::Header>(received);
        auto const* coe     = pointData<CoE::Header>(header);
        auto const* sdo     = pointData<CoE::ServiceData>(coe);
        auto const* payload = pointData<uint8_t>(sdo);

        // skip gateway message
        if ((header->address & mailbox::GATEWAY_MESSAGE_MASK) != 0)
        {
            return ProcessingResult::NOOP;
        }

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
        if ((sdo_->command == CoE::SDO::request::UPLOAD) or (sdo_->command == CoE::SDO::request::DOWNLOAD))
        {
            if ((sdo->index != sdo_->index) or (sdo->subindex != sdo_->subindex))
            {
                return ProcessingResult::NOOP;
            }
        }

        // Message IS related: same index/subindex and right mailbox session handle -> low risk of collision
        // => check if message response is coherent
        if (sdo->command == CoE::SDO::request::ABORT)
        {
            uint32_t code;
            std::memcpy(&code, payload, sizeof(uint32_t));
            // TODO: let client display itself the message
            coe_info("Abort requested for %x:%d ! code %08" PRIx32" - %s\n", sdo->index, sdo->subindex, code, CoE::SDO::abort_to_str(code));
            status_ = code;
            return ProcessingResult::FINALIZE;
        }

        // everything is fine: process the payload
        switch (sdo_->command)
        {
            case CoE::SDO::request::UPLOAD:               { return processUpload(header, sdo, payload);            }
            case CoE::SDO::request::UPLOAD_SEGMENTED:     { return processUploadSegmented(header, sdo, payload);   }
            case CoE::SDO::request::DOWNLOAD:             { return processDownload(header, sdo, payload);          }
            case CoE::SDO::request::DOWNLOAD_SEGMENTED:   { return processDownloadSegmented(header, sdo, payload); }
            default:
            {
                status_ = MessageStatus::COE_UNKNOWN_SERVICE;
                return ProcessingResult::FINALIZE;
            }
        }
    }


    ProcessingResult SDOMessage::processUpload(mailbox::Header const* header, CoE::ServiceData const* sdo, uint8_t const* payload)
    {
        if (sdo->command != CoE::SDO::response::UPLOAD)
        {
            status_ = MessageStatus::COE_WRONG_SERVICE;
            return ProcessingResult::FINALIZE;
        }

        if (sdo->transfer_type == 1)
        {
            // expedited transfer
            uint32_t size = 4 - sdo->block_size;
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

        uint32_t complete_size;
        std::memcpy(&complete_size, payload, sizeof(uint32_t));
        payload += 4;

        if (*client_data_size_ < complete_size)
        {
            status_ = MessageStatus::COE_CLIENT_BUFFER_TOO_SMALL;
            return ProcessingResult::FINALIZE;
        }

        uint32_t const data_len = header->len - 10;
        if (data_len >= complete_size)
        {
            // standard
            std::memcpy(client_data_, payload, complete_size);
            *client_data_size_ = complete_size;

            status_ = MessageStatus::SUCCESS;
            return ProcessingResult::FINALIZE;
        }

        // segmented
        uint32_t size;
        std::memcpy(&size, payload, sizeof(uint32_t));
        payload += 4;

        std::memcpy(client_data_, payload, size);
        client_data_ += size;
        *client_data_size_ = size;

        // since transfer is segmented, we need to request the next segment
        coe_->service = CoE::Service::SDO_REQUEST;
        sdo_->command = CoE::SDO::request::UPLOAD_SEGMENTED;
        sdo_->complete_access = false; // use for toggle bit - first segment shall be set to 0
        sdo_->block_size      = 0;
        sdo_->transfer_type   = 0;
        sdo_->size_indicator  = 0;
        status_ = MessageStatus::RUNNING;
        return ProcessingResult::CONTINUE;
    }

    ProcessingResult SDOMessage::processUploadSegmented(mailbox::Header const* header, CoE::ServiceData const* sdo, uint8_t const* payload)
    {
        if (sdo->command != CoE::SDO::response::UPLOAD_SEGMENTED)
        {
            status_ = MessageStatus::COE_WRONG_SERVICE;
            return ProcessingResult::FINALIZE;
        }

        if (sdo->complete_access != sdo_->complete_access)
        {
            status_ = MessageStatus::COE_SEGMENT_BAD_TOGGLE_BIT;
            return ProcessingResult::FINALIZE;
        }

        uint32_t size = 0;
        if (header->len == 10)
        {
            size = 7 - (sdo->block_size | (sdo->size_indicator << 2));
        }
        else
        {
            std::memcpy(&size, payload, sizeof(uint32_t));
            payload += sizeof(uint32_t);
        }

        std::memcpy(client_data_, payload, size);
        client_data_ += size;
        *client_data_size_ += size;

        bool more_follow = sdo->size_indicator;
        if (not more_follow)
        {
            status_ = MessageStatus::SUCCESS;
            return ProcessingResult::FINALIZE;
        }

        sdo_->complete_access = not sdo_->complete_access;
        return ProcessingResult::CONTINUE;
    }

    ProcessingResult SDOMessage::processDownload(mailbox::Header const*, CoE::ServiceData const* sdo, uint8_t const*)
    {
        if (sdo->command != CoE::SDO::response::DOWNLOAD)
        {
            status_ = MessageStatus::COE_WRONG_SERVICE;
            return ProcessingResult::FINALIZE;
        }

        status_ = MessageStatus::SUCCESS; // all checks passed
        return ProcessingResult::FINALIZE;
    }

    ProcessingResult SDOMessage::processDownloadSegmented(mailbox::Header const*, CoE::ServiceData const* sdo, uint8_t const*)
    {
        if (sdo->command != CoE::SDO::response::DOWNLOAD_SEGMENTED)
        {
            status_ = MessageStatus::COE_WRONG_SERVICE;
            return ProcessingResult::FINALIZE;
        }

        return ProcessingResult::FINALIZE;
    }


    SDOInformationMessage::SDOInformationMessage(uint16_t mailbox_size, uint8_t request, void* data, uint32_t* data_size,
                                                 uint32_t request_payload_size, nanoseconds timeout)
        : AbstractMessage(mailbox_size, timeout)
        , client_data_(reinterpret_cast<uint8_t*>(data))
        , client_data_size_(data_size)
    {
        coe_ = pointData<CoE::Header>(header_);
        sdo_ = pointData<CoE::ServiceDataInfo>(coe_);
        payload_ = pointData<uint8_t>(sdo_);

        header_->len      = 6 + request_payload_size;
        header_->priority = 0; // unused
        header_->channel  = 0;
        header_->type     = mailbox::Type::CoE;
        header_->reserved = 0;

        coe_->number   = 0;
        coe_->reserved = 0;
        coe_->service  = CoE::Service::SDO_INFORMATION;

        sdo_->opcode         = request & 0x7F;
        sdo_->incomplete     = 0;
        sdo_->reserved       = 0;
        sdo_->fragments_left = 0;

        std::memcpy(payload_, data, request_payload_size);
    }


    ProcessingResult SDOInformationMessage::process(uint8_t const* received)
    {
        auto const* header  = pointData<mailbox::Header>(received);
        auto const* coe     = pointData<CoE::Header>(header);
        auto const* sdo     = pointData<CoE::ServiceDataInfo>(coe);
        auto const* payload = pointData<uint8_t>(sdo);

        // skip gateway message
        if ((header->address & mailbox::GATEWAY_MESSAGE_MASK) != 0)
        {
            return ProcessingResult::NOOP;
        }

        // check if the received message is related to this one
        if (header->type != mailbox::Type::CoE)
        {
            return ProcessingResult::NOOP;
        }

        if ((coe->service != CoE::Service::SDO_INFORMATION))
        {
            return ProcessingResult::NOOP;
        }

        // Message IS related: same index/subindex and right mailbox session handle -> low risk of collision
        // => check if message response is coherent
        if (sdo->opcode == CoE::SDO::information::SDO_INFO_ERROR_REQ)
        {
            uint32_t code = *reinterpret_cast<uint32_t const*>(payload);
            // TODO: let client display itself the message
            coe_info("Abort requested for sdo information ! code %08x - %s\n", code, CoE::SDO::abort_to_str(code));
            status_ = code;
            return ProcessingResult::FINALIZE;
        }

        // everything is fine: process the payload
        switch (sdo_->opcode)
        {
            case CoE::SDO::information::GET_OD_LIST_REQ : { return processResponse(header, sdo, payload, CoE::SDO::information::GET_OD_LIST_RESP); }
            case CoE::SDO::information::GET_OD_REQ      : { return processResponse(header, sdo, payload, CoE::SDO::information::GET_OD_RESP);      }
            case CoE::SDO::information::GET_ED_REQ      : { return processResponse(header, sdo, payload, CoE::SDO::information::GET_ED_RESP);      }
            default:
            {
                status_ = MessageStatus::COE_UNKNOWN_SERVICE;
                return ProcessingResult::FINALIZE;
            }
        }
    }

    ProcessingResult SDOInformationMessage::processResponse(mailbox::Header const* header, CoE::ServiceDataInfo const* sdo,
                                                            uint8_t const* payload, uint8_t expected_opcode)
    {
        if (sdo->opcode != expected_opcode)
        {
            status_ = MessageStatus::COE_WRONG_SERVICE;
            return ProcessingResult::FINALIZE;
        }

        int32_t size = header->len - sizeof(CoE::ServiceDataInfo) - sizeof(CoE::Header);
        int32_t remaining_size = *client_data_size_ - already_received_size_;

        coe_info("\nReceived size %i already received %i, remaining_size %i  client_data_ %p \n ", size , already_received_size_, remaining_size, client_data_);
        if (size < 0)
        {
            coe_error("\nMessage size if ill-formed %i\n ", size);
            status_ = CoE::SDO::abort::GENERAL_ERROR;
            return ProcessingResult::FINALIZE;
        }

        if(remaining_size < size)
        {
            status_ = MessageStatus::COE_CLIENT_BUFFER_TOO_SMALL;
        }
        else
        {
            std::memcpy(client_data_, payload, size);
            client_data_ += size;
        }
        already_received_size_ += size;


        if (sdo->fragments_left > 0)
        {
            return ProcessingResult::FINALIZE_AND_KEEP;
        }

        if (already_received_size_ < *client_data_size_)
        {
            status_ = MessageStatus::SUCCESS;
            *client_data_size_ = already_received_size_;
        }

        return ProcessingResult::FINALIZE;
    }

    EmergencyMessage::EmergencyMessage(Mailbox& mailbox)
        : AbstractMessage(mailbox.recv_size, 0ns)
        , mailbox_{mailbox}
    { }

    ProcessingResult EmergencyMessage::process(uint8_t const* received)
    {
        // check if the received message is an emergency one
        auto const* header = pointData<mailbox::Header>(received);
        if (header->type != mailbox::Type::CoE)
        {
            return ProcessingResult::NOOP;
        }

        auto const* coe = pointData<CoE::Header>(header);
        if (coe->service != CoE::Service::EMERGENCY)
        {
            return ProcessingResult::NOOP;
        }

        auto const* emg = pointData<CoE::Emergency>(coe);
        mailbox_.emergencies.push_back(*emg);
        return ProcessingResult::FINALIZE_AND_KEEP;
    }
}