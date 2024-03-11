#include <cstring>
<<<<<<< HEAD:lib/src/CoE/mailbox/request.cc
=======
#include <algorithm>
>>>>>>> Move mailbox in common part again, but add namespace to split request/response sides.:lib/src/Mailbox.cc
#include <cinttypes>

#include "debug.h"
<<<<<<< HEAD:lib/src/CoE/mailbox/request.cc
#include "kickcat/CoE/mailbox/request.h"
=======
#include "CoE/Mailbox.h"
>>>>>>> Move mailbox in common part again, but add namespace to split request/response sides.:lib/src/Mailbox.cc

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
<<<<<<< HEAD:lib/src/CoE/mailbox/request.cc
}
=======


    GatewayMessage::GatewayMessage(uint16_t mailbox_size, uint8_t const* raw_message, uint16_t gateway_index, nanoseconds timeout)
        : AbstractMessage(mailbox_size, timeout)
    {
        auto const* header = pointData<mailbox::Header>(raw_message);

        // Copy raw message in internal data field
        int32_t size = sizeof(mailbox::Header) + header->len;
        std::memcpy(data_.data(), raw_message, size);

        // Store gateway index to associate the reply with the request
        gateway_index_ = gateway_index;

        // Switch address field with gateway index and identifier
        address_ = header_->address;
        header_->address = mailbox::GATEWAY_MESSAGE_MASK | gateway_index;
    }


    ProcessingResult GatewayMessage::process(uint8_t const* received)
    {
        auto const* header = pointData<mailbox::Header>(received);

        // Check if the message is associated to our gateway request
        if (header->address != header_->address)
        {
            return ProcessingResult::NOOP;
        }

        // It is the reply to this request: store the result and set back the address field
        int32_t size = header->len + sizeof(mailbox::Header);
        data_.resize(size);
        std::memcpy(data_.data(), received, size);

        header_->address = address_;

        status_ = MessageStatus::SUCCESS;
        return ProcessingResult::FINALIZE;
    }
}


namespace kickcat::mailbox::response
{
    Mailbox::Mailbox(AbstractESC* esc, SyncManagerConfig mbx_in, SyncManagerConfig mbx_out, uint16_t max_msgs)
        : esc_{esc}
        , mbx_in_{mbx_in}
        , mbx_out_{mbx_out}
        , max_msgs_{max_msgs}
    {

    }

    void Mailbox::receive()
    {
        std::vector<uint8_t> raw_message;
        raw_message.resize(mbx_in_.length);
        int32_t read_bytes = esc_->read(mbx_in_.start_address, raw_message.data(), mbx_in_.length);
        if (read_bytes != mbx_in_.length)
        {
            return;
        }

        auto const* header  = pointData<mailbox::Header>(raw_message.data());
        if ((header->type == mailbox::ERR) or (header->len == 0))
        {
            replyError(std::move(raw_message), mailbox::Error::INVALID_HEADER);
            return;
        }

        for (auto it = to_process_.begin(); it != to_process_.end(); ++it)
        {
            ProcessingResult state = (*it)->process(raw_message);
            switch (state)
            {
                case ProcessingResult::NOOP:
                {
                    continue;
                }
                case ProcessingResult::CONTINUE:
                case ProcessingResult::FINALIZE_AND_KEEP:
                {
                    return;
                }
                case ProcessingResult::FINALIZE:
                {
                    it = to_process_.erase(it);
                    return;
                }
                default: { }
            }
        }

        if (to_process_.size() >= max_msgs_)
        {
            // Queue is full and no one process it: drop the message
            replyError(std::move(raw_message), mailbox::Error::NO_MORE_MEMORY);
            return;
        }

        // No message handle it: let's try to build a new one
        for (auto& factory : factories_)
        {
            auto msg = factory(this, std::move(raw_message));
            if (msg != nullptr)
            {
                to_process_.push_back(msg);
                return;
            }
        }

        // Nothing can be done with this message
        replyError(std::move(raw_message), mailbox::Error::UNSUPPORTED_PROTOCOL);
    }


    void Mailbox::process()
    {
        for (auto it = to_process_.begin(); it != to_process_.end(); ++it)
        {
            ProcessingResult state = (*it)->process();
            switch (state)
            {
                case ProcessingResult::NOOP:
                {
                    continue;
                }
                case ProcessingResult::CONTINUE:
                case ProcessingResult::FINALIZE_AND_KEEP:
                {
                    return;
                }
                case ProcessingResult::FINALIZE:
                {
                    it = to_process_.erase(it);
                    return;
                }
                default: { }
            }
        }
    }

    void Mailbox::send()
    {
        if (to_send_.empty())
        {
            return;
        }

        auto& msg = to_send_.front();
        int32_t written_bytes = esc_->write(mbx_out_.start_address, msg.data(), msg.size());
        if (written_bytes > 0)
        {
            to_send_.pop();
        }
    }

    void Mailbox::enableCoE()
    {
        factories_.push_back(&CoE::createSDOMessage);
    }

    void Mailbox::replyError(std::vector<uint8_t>&& raw_message, uint16_t code)
    {
        auto* header  = pointData<mailbox::Header>(raw_message.data());
        auto* err     = pointData<mailbox::Error::ServiceData>(header);

        header->type = mailbox::ERR;
        header->len  = sizeof(mailbox::Error::ServiceData);
        err->type    = 0x1;
        err->detail  = code;

        to_send_.push(std::move(raw_message));
    }

    AbstractMessage::AbstractMessage(Mailbox* mbx)
        : mailbox_{mbx}
    {

    }

    uint16_t AbstractMessage::replyExpectedSize()
    {
        return mailbox_->mbx_out_.length;
    }

    void AbstractMessage::reply(std::vector<uint8_t>&& reply)
    {
        mailbox_->to_send_.push(std::move(reply));
    }

    void AbstractMessage::replyError(std::vector<uint8_t>&& raw_message, uint16_t code)
    {
        mailbox_->replyError(std::move(raw_message), code);
    }
}
>>>>>>> Move mailbox in common part again, but add namespace to split request/response sides.:lib/src/Mailbox.cc
