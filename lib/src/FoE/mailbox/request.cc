#include <cstring>

#include "debug.h"
#include "Error.h"
#include "kickcat/FoE/mailbox/request.h"

namespace kickcat::mailbox::request
{
    namespace
    {
        constexpr size_t DATA_OVERHEAD = sizeof(mailbox::Header) + sizeof(FoE::Header) + sizeof(FoE::data::Header);

        void initFoEHeader(mailbox::Header* header, FoE::Header* foe, uint8_t opcode)
        {
            header->priority = 0;
            header->channel  = 0;
            header->reserved = 0;
            header->type     = mailbox::Type::FoE;
            foe->opcode      = opcode;
            foe->reserved    = 0;
        }

        // Common reply dispatch: returns true and sets status_/result when the reply is an FoE
        // ERROR (terminal) or BUSY (retry); the caller handles the protocol-specific opcodes.
        bool handleErrorOrBusy(FoE::Header const* foe, uint16_t len, uint32_t& status, bool& retry)
        {
            retry = false;
            if (foe->opcode == FoE::opcode::ERROR)
            {
                if (len < (sizeof(FoE::Header) + sizeof(FoE::error::Header)))
                {
                    status = MessageStatus::FOE_WRONG_SERVICE;
                    return true;
                }
                auto const* err = pointData<FoE::error::Header>(foe);
                status = MessageStatus::FOE_RESULT | (err->error_code & 0xFFFF);
                return true;
            }
            if (foe->opcode == FoE::opcode::BUSY)
            {
                retry = true; // re-send whatever is still in data_ (last Ack / Data / request)
                return true;
            }
            return false;
        }
    }

    FoEReadMessage::FoEReadMessage(uint16_t mailbox_size, uint16_t reply_size, std::string const& file_name,
                                   uint32_t password, void* data, uint32_t* data_size, nanoseconds timeout)
        : AbstractMessage(mailbox_size, timeout)
        , client_data_(reinterpret_cast<uint8_t*>(data))
        , client_data_size_(data_size)
        , client_buffer_size_(*data_size)
        , max_data_(static_cast<uint32_t>(reply_size - DATA_OVERHEAD))
    {
        auto* foe  = pointData<FoE::Header>(header_);
        auto* rd   = pointData<FoE::read::Header>(foe);
        auto* name = pointData<uint8_t>(rd);

        initFoEHeader(header_, foe, FoE::opcode::READ);
        rd->password = password;
        std::memcpy(name, file_name.data(), file_name.size());
        header_->len = static_cast<uint16_t>(sizeof(FoE::Header) + sizeof(FoE::read::Header) + file_name.size());

        *client_data_size_ = 0;
    }

    ProcessingResult FoEReadMessage::process(uint8_t const* received)
    {
        auto const* header = pointData<mailbox::Header>(received);
        if ((header->address & mailbox::GATEWAY_MESSAGE_MASK) != 0)
        {
            return ProcessingResult::NOOP;
        }
        if (header->type != mailbox::Type::FoE)
        {
            return ProcessingResult::NOOP;
        }

        auto const* foe = pointData<FoE::Header>(header);

        bool retry = false;
        if (handleErrorOrBusy(foe, header->len, status_, retry))
        {
            if (retry)
            {
                return ProcessingResult::CONTINUE;
            }
            return ProcessingResult::FINALIZE;
        }

        if (foe->opcode != FoE::opcode::DATA)
        {
            return ProcessingResult::NOOP;
        }

        if (header->len < (sizeof(FoE::Header) + sizeof(FoE::data::Header)))
        {
            status_ = MessageStatus::FOE_WRONG_SERVICE;
            return ProcessingResult::FINALIZE;
        }

        auto const* dh      = pointData<FoE::data::Header>(foe);
        auto const* payload = pointData<uint8_t>(dh);
        uint32_t data_len   = static_cast<uint32_t>(header->len - sizeof(FoE::Header) - sizeof(FoE::data::Header));

        if (dh->packet_number != expected_packet_)
        {
            status_ = MessageStatus::FOE_PACKET_NUMBER;
            return ProcessingResult::FINALIZE;
        }

        if (data_len > (client_buffer_size_ - received_size_))
        {
            status_ = MessageStatus::FOE_CLIENT_BUFFER_TOO_SMALL;
            return ProcessingResult::FINALIZE;
        }
        std::memcpy(client_data_ + received_size_, payload, data_len);
        received_size_ += data_len;

        bool last = (data_len < max_data_); // a packet shorter than the max ends the transfer
        return sendAck(dh->packet_number, last);
    }

    ProcessingResult FoEReadMessage::sendAck(uint32_t packet_number, bool last)
    {
        auto* foe = pointData<FoE::Header>(header_);
        auto* ah  = pointData<FoE::ack::Header>(foe);

        initFoEHeader(header_, foe, FoE::opcode::ACK);
        ah->packet_number = packet_number;
        header_->len = sizeof(FoE::Header) + sizeof(FoE::ack::Header);

        if (last)
        {
            *client_data_size_ = received_size_;
            // The final Ack is unacknowledged: setting SUCCESS before CONTINUE makes send() emit it
            // once without re-queueing the message for a reply that will never come.
            status_ = MessageStatus::SUCCESS;
            return ProcessingResult::CONTINUE;
        }

        expected_packet_++;
        return ProcessingResult::CONTINUE;
    }


    FoEWriteMessage::FoEWriteMessage(uint16_t mailbox_size, std::string const& file_name, uint32_t password,
                                     void const* data, uint32_t data_size, nanoseconds timeout)
        : AbstractMessage(mailbox_size, timeout)
        , client_data_(reinterpret_cast<uint8_t const*>(data))
        , client_data_size_(data_size)
        , max_data_(static_cast<uint32_t>(data_.size() - DATA_OVERHEAD))
    {
        auto* foe  = pointData<FoE::Header>(header_);
        auto* wr   = pointData<FoE::write::Header>(foe);
        auto* name = pointData<uint8_t>(wr);

        initFoEHeader(header_, foe, FoE::opcode::WRITE);
        wr->password = password;
        std::memcpy(name, file_name.data(), file_name.size());
        header_->len = static_cast<uint16_t>(sizeof(FoE::Header) + sizeof(FoE::write::Header) + file_name.size());
    }

    ProcessingResult FoEWriteMessage::process(uint8_t const* received)
    {
        auto const* header = pointData<mailbox::Header>(received);
        if ((header->address & mailbox::GATEWAY_MESSAGE_MASK) != 0)
        {
            return ProcessingResult::NOOP;
        }
        if (header->type != mailbox::Type::FoE)
        {
            return ProcessingResult::NOOP;
        }

        auto const* foe = pointData<FoE::Header>(header);

        bool retry = false;
        if (handleErrorOrBusy(foe, header->len, status_, retry))
        {
            if (retry)
            {
                return ProcessingResult::CONTINUE;
            }
            return ProcessingResult::FINALIZE;
        }

        if (foe->opcode != FoE::opcode::ACK)
        {
            return ProcessingResult::NOOP;
        }

        if (header->len < (sizeof(FoE::Header) + sizeof(FoE::ack::Header)))
        {
            status_ = MessageStatus::FOE_WRONG_SERVICE;
            return ProcessingResult::FINALIZE;
        }
        if (pointData<FoE::ack::Header>(foe)->packet_number != packet_number_)
        {
            status_ = MessageStatus::FOE_PACKET_NUMBER;
            return ProcessingResult::FINALIZE;
        }

        if (last_sent_)
        {
            status_ = MessageStatus::SUCCESS;
            return ProcessingResult::FINALIZE;
        }

        // Ack of the Write Request (packet 0) or of a Data packet: send the next one.
        prepareData();
        return ProcessingResult::CONTINUE;
    }

    void FoEWriteMessage::prepareData()
    {
        uint32_t remaining = client_data_size_ - sent_size_;
        uint32_t chunk = remaining;
        if (chunk > max_data_)
        {
            chunk = max_data_;
        }

        auto* foe     = pointData<FoE::Header>(header_);
        auto* dh      = pointData<FoE::data::Header>(foe);
        auto* payload = pointData<uint8_t>(dh);

        initFoEHeader(header_, foe, FoE::opcode::DATA);
        packet_number_++;
        dh->packet_number = packet_number_;
        if (chunk > 0)
        {
            std::memcpy(payload, client_data_ + sent_size_, chunk);
        }
        sent_size_ += chunk;
        header_->len = static_cast<uint16_t>(sizeof(FoE::Header) + sizeof(FoE::data::Header) + chunk);

        if (chunk < max_data_)
        {
            // a packet shorter than the max (possibly empty) signals the last one
            last_sent_ = true;
        }
    }


    std::shared_ptr<AbstractMessage> Mailbox::createFoERead(std::string const& file_name, uint32_t password,
                                                            void* data, uint32_t* data_size, nanoseconds timeout)
    {
        if (recv_size == 0)
        {
            THROW_ERROR("This mailbox is inactive");
        }
        constexpr size_t overhead = sizeof(mailbox::Header) + sizeof(FoE::Header) + sizeof(FoE::read::Header);
        if ((overhead + file_name.size()) > recv_size)
        {
            THROW_ERROR("File name too long for the mailbox");
        }
        if (send_size < (sizeof(mailbox::Header) + sizeof(FoE::Header) + sizeof(FoE::data::Header)))
        {
            THROW_ERROR("Mailbox too small for FoE");
        }
        auto msg = std::make_shared<FoEReadMessage>(recv_size, send_size, file_name, password, data, data_size, timeout);
        msg->setCounter(nextCounter());
        to_send.push(msg);
        return msg;
    }

    std::shared_ptr<AbstractMessage> Mailbox::createFoEWrite(std::string const& file_name, uint32_t password,
                                                             void const* data, uint32_t data_size, nanoseconds timeout)
    {
        if (recv_size == 0)
        {
            THROW_ERROR("This mailbox is inactive");
        }
        constexpr size_t overhead = sizeof(mailbox::Header) + sizeof(FoE::Header) + sizeof(FoE::write::Header);
        if ((overhead + file_name.size()) > recv_size)
        {
            THROW_ERROR("File name too long for the mailbox");
        }
        if (recv_size < (sizeof(mailbox::Header) + sizeof(FoE::Header) + sizeof(FoE::data::Header)))
        {
            THROW_ERROR("Mailbox too small for FoE");
        }
        auto msg = std::make_shared<FoEWriteMessage>(recv_size, file_name, password, data, data_size, timeout);
        msg->setCounter(nextCounter());
        to_send.push(msg);
        return msg;
    }
}
