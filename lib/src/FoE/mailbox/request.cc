#include <algorithm>
#include <cstring>
#include <limits>

#include "Error.h"
#include "kickcat/FoE/mailbox/request.h"

namespace kickcat::mailbox::request
{
    FoEMessage::FoEMessage(uint16_t mbx_recv_size, uint16_t mbx_send_size, uint8_t opcode, std::string const& name,
                           uint32_t password, std::vector<uint8_t> file, nanoseconds timeout)
        : AbstractMessage(mbx_recv_size, mbx_send_size, timeout)
        , reading_{opcode == FoE::opcode::READ}
        , file_{std::move(file)}
    {
        if (name.empty())
        {
            THROW_ERROR("FoE file name is empty");
        }
        if (data_.size() < (FoE::OVERHEAD + name.size()))
        {
            THROW_ERROR("Mailbox is too small to hold the FoE request");
        }
        if (file_.size() > std::numeric_limits<uint32_t>::max())
        {
            THROW_ERROR("FoE file too big: 4 GiB at most");
        }

        foe_     = pointData<FoE::Header>(header_);
        payload_ = pointData<uint8_t>(foe_);

        header_->priority = 0;
        header_->channel  = 0;
        header_->type     = mailbox::Type::FoE;

        prepare(opcode, password, static_cast<uint16_t>(name.size()));
        std::memcpy(payload_, name.data(), name.size());
    }


    void FoEMessage::prepare(uint8_t opcode, uint32_t value, uint16_t payload_size)
    {
        header_->len   = static_cast<uint16_t>(sizeof(FoE::Header) + payload_size);
        foe_->opcode   = opcode;
        foe_->reserved = 0;
        foe_->value    = value;
    }


    void FoEMessage::prepareData()
    {
        uint32_t capacity = static_cast<uint32_t>(data_.size()) - FoE::OVERHEAD;
        uint32_t chunk = std::min(static_cast<uint32_t>(file_.size()) - offset_, capacity);

        ++packet_;
        prepare(FoE::opcode::DATA, packet_, static_cast<uint16_t>(chunk));
        std::copy_n(file_.data() + offset_, chunk, payload_);   // memcpy is undefined on an empty file
        offset_ += chunk;

        // A full packet does not end the transfer: a file whose size is a multiple of the capacity ends
        // with an empty packet (ETG.1020 chapter 18.1).
        last_ = (chunk < capacity);
    }


    uint32_t FoEMessage::bytesTransferred() const
    {
        if (reading_)
        {
            return static_cast<uint32_t>(file_.size());
        }
        return acknowledged_;
    }


    ProcessingResult FoEMessage::abort(uint32_t code, uint32_t status)
    {
        prepare(FoE::opcode::ERROR, code, 0);
        pending_status_ = status;
        rearmTimeout();
        return ProcessingResult::CONTINUE;
    }


    void FoEMessage::sent()
    {
        if (cancelled_ and (status_ == MessageStatus::RUNNING))
        {
            // The PDU is written on the bus from data_ after this call: replace it by the error
            prepare(FoE::opcode::ERROR, FoE::result::NOT_DEFINED, 0);
            pending_status_ = MessageStatus::FOE_CANCELLED;
        }

        if (pending_status_ != MessageStatus::RUNNING)
        {
            status_ = pending_status_;
        }
    }


    ProcessingResult FoEMessage::process(uint8_t const* received)
    {
        auto const* header = pointData<mailbox::Header>(received);
        if (header->address & mailbox::GATEWAY_MESSAGE_MASK)
        {
            return ProcessingResult::NOOP;
        }

        if (header->type != mailbox::Type::FoE)
        {
            return ProcessingResult::NOOP;
        }

        if ((header->len < sizeof(FoE::Header)) or ((sizeof(mailbox::Header) + header->len) > send_size_))
        {
            status_ = MessageStatus::FOE_INVALID_REPLY;
            return ProcessingResult::FINALIZE;
        }

        auto const* foe = pointData<FoE::Header>(header);
        uint32_t value         = foe->value;
        uint8_t const* payload = pointData<uint8_t>(foe);
        uint16_t payload_size  = static_cast<uint16_t>(header->len - sizeof(FoE::Header));

        if (foe->opcode == FoE::opcode::ERROR)
        {
            status_ = FoE::normalizeError(value);
            error_text_.assign(reinterpret_cast<char const*>(payload), payload_size);
            return ProcessingResult::FINALIZE;
        }

        if (cancelled_)
        {
            return abort(FoE::result::NOT_DEFINED, MessageStatus::FOE_CANCELLED);
        }

        if (reading_)
        {
            return processRead(foe->opcode, value, payload, payload_size);
        }
        return processWrite(foe->opcode, value);
    }


    ProcessingResult FoEMessage::processRead(uint8_t opcode, uint32_t value, uint8_t const* payload, uint16_t payload_size)
    {
        switch (opcode)
        {
            case FoE::opcode::DATA:
            {
                if (value != (packet_ + 1))
                {
                    return abort(FoE::result::PACKET_NUMBER_WRONG, MessageStatus::FOE_PACKET_NUMBER_WRONG);
                }

                file_.insert(file_.end(), payload, payload + payload_size);
                packet_ = value;
                if (payload_size < (send_size_ - FoE::OVERHEAD))
                {
                    // The slave waits for this last ACK: the transfer is done once it is sent.
                    pending_status_ = MessageStatus::SUCCESS;
                }
                prepare(FoE::opcode::ACK, packet_, 0);
                rearmTimeout();
                return ProcessingResult::CONTINUE;
            }
            case FoE::opcode::BUSY:
            {
                prepare(FoE::opcode::ACK, packet_, 0);
                rearmTimeout();
                return ProcessingResult::CONTINUE;
            }
            default:
            {
                return abort(FoE::result::ILLEGAL, MessageStatus::FOE_UNEXPECTED_OPCODE);
            }
        }
    }


    ProcessingResult FoEMessage::processWrite(uint8_t opcode, uint32_t value)
    {
        switch (opcode)
        {
            case FoE::opcode::ACK:
            {
                if (value != packet_)
                {
                    return abort(FoE::result::PACKET_NUMBER_WRONG, MessageStatus::FOE_PACKET_NUMBER_WRONG);
                }

                acknowledged_ = offset_;
                if (last_)
                {
                    status_ = MessageStatus::SUCCESS;
                    return ProcessingResult::FINALIZE;
                }
                prepareData();
                rearmTimeout();
                return ProcessingResult::CONTINUE;
            }
            case FoE::opcode::BUSY:
            {
                // Repeat the last PDU, still in data_ (ETG.1000.5 chapter 6.1.6.3.6)
                rearmTimeout();
                return ProcessingResult::CONTINUE;
            }
            default:
            {
                return abort(FoE::result::ILLEGAL, MessageStatus::FOE_UNEXPECTED_OPCODE);
            }
        }
    }
}
