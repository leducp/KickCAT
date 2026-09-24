#include <cstring>

#include "kickcat/CoE/mailbox/response.h"
#include "kickcat/FoE/mailbox/response.h"

namespace kickcat::mailbox::response
{
    std::shared_ptr<AbstractMessage> createFoEMessage(Mailbox* mbx, std::vector<uint8_t>&& raw_message)
    {
        auto const* header = pointData<mailbox::Header>(raw_message.data());
        if (header->type != mailbox::Type::FoE)
        {
            return nullptr;
        }

        if (header->len < sizeof(FoE::Header))
        {
            return std::make_shared<MailboxErrorMessage>(mbx, std::move(raw_message), mailbox::Error::INVALID_SIZE);
        }

        return std::make_shared<FoEMessage>(mbx, std::move(raw_message));
    }


    FoEMessage::FoEMessage(Mailbox* mbx, std::vector<uint8_t>&& raw_message)
        : AbstractMessage{mbx}
        , storage_{mbx->storage()}
    {
        data_ = std::move(raw_message);
    }


    ProcessingResult FoEMessage::process()
    {
        if (data_.empty())
        {
            // The request has been handled: the next exchanges come through process(raw_message)
            return ProcessingResult::NOOP;
        }

        std::vector<uint8_t> request = std::move(data_);
        data_.clear();
        return start(request);
    }


    ProcessingResult FoEMessage::process(std::vector<uint8_t> const& raw_message)
    {
        if (not data_.empty())
        {
            return ProcessingResult::NOOP;
        }

        auto const* header = pointData<mailbox::Header>(raw_message.data());
        if (header->type != mailbox::Type::FoE)
        {
            return ProcessingResult::NOOP;
        }

        if (header->len < sizeof(FoE::Header))
        {
            replyError(std::vector<uint8_t>(raw_message), mailbox::Error::INVALID_SIZE);
            return ProcessingResult::FINALIZE_AND_KEEP;
        }

        auto const* foe = pointData<FoE::Header>(header);
        uint32_t value         = foe->value;
        uint8_t const* payload = pointData<uint8_t>(foe);
        uint16_t payload_size  = static_cast<uint16_t>(header->len - sizeof(FoE::Header));

        switch (foe->opcode)
        {
            case FoE::opcode::READ:
            case FoE::opcode::WRITE:
            {
                // A new request supersedes the current transfer (e.g. the master timed out and retries)
                endTransfer();
                return start(raw_message);
            }
            case FoE::opcode::ERROR:
            {
                endTransfer();
                return ProcessingResult::FINALIZE;
            }
            default:
            {
                break;
            }
        }

        if (reader_)
        {
            return readSession(foe->opcode, value);
        }
        if (writer_)
        {
            return writeSession(foe->opcode, value, payload, payload_size);
        }
        return fail(FoE::result::PACKET_NUMBER_WRONG);
    }


    ProcessingResult FoEMessage::start(std::vector<uint8_t> const& raw_message)
    {
        mailbox_size_ = raw_message.size();

        auto const* header = pointData<mailbox::Header>(raw_message.data());
        auto const* foe     = pointData<FoE::Header>(header);
        uint32_t value      = foe->value;
        char const* payload = reinterpret_cast<char const*>(pointData<uint8_t>(foe));
        std::string_view name{payload, static_cast<std::size_t>(header->len - sizeof(FoE::Header))};

        switch (foe->opcode)
        {
            case FoE::opcode::READ:
            {
                auto [rc, reader] = storage_.openRead(name, value);
                if (rc != 0)
                {
                    return fail(rc);
                }
                reader_ = std::move(reader);
                packet_ = 0;
                return sendData();
            }
            case FoE::opcode::WRITE:
            {
                auto [rc, writer] = storage_.openWrite(name, value);
                if (rc != 0)
                {
                    return fail(rc);
                }
                writer_ = std::move(writer);
                packet_ = 0;
                reply(createPDU(FoE::opcode::ACK, 0, 0));
                return ProcessingResult::FINALIZE_AND_KEEP;
            }
            case FoE::opcode::ERROR:
            {
                return ProcessingResult::FINALIZE;
            }
            default:
            {
                // DATA, ACK or BUSY without a transfer in progress
                return fail(FoE::result::PACKET_NUMBER_WRONG);
            }
        }
    }


    ProcessingResult FoEMessage::readSession(uint8_t opcode, uint32_t value)
    {
        if (opcode != FoE::opcode::ACK)
        {
            return fail(FoE::result::ILLEGAL);
        }

        if (value != packet_)
        {
            return fail(FoE::result::PACKET_NUMBER_WRONG);
        }

        if (last_)
        {
            endTransfer();
            return ProcessingResult::FINALIZE;
        }

        return sendData();
    }


    ProcessingResult FoEMessage::writeSession(uint8_t opcode, uint32_t value, uint8_t const* payload, uint16_t payload_size)
    {
        if (opcode != FoE::opcode::DATA)
        {
            return fail(FoE::result::ILLEGAL);
        }

        if (value != (packet_ + 1))
        {
            return fail(FoE::result::PACKET_NUMBER_WRONG);
        }

        uint32_t rc = writer_->write(payload, payload_size);
        if (rc != 0)
        {
            return fail(rc);
        }
        packet_ = value;

        if (payload_size == capacity())
        {
            reply(createPDU(FoE::opcode::ACK, packet_, 0));
            return ProcessingResult::FINALIZE_AND_KEEP;
        }

        rc = writer_->commit();
        endTransfer();
        if (rc != 0)
        {
            return fail(rc);
        }
        reply(createPDU(FoE::opcode::ACK, packet_, 0));
        return ProcessingResult::FINALIZE;
    }


    ProcessingResult FoEMessage::sendData()
    {
        auto pdu = createPDU(FoE::opcode::DATA, packet_ + 1, 0);
        uint32_t size = 0;
        uint32_t rc = reader_->read(pdu.data() + FoE::OVERHEAD, capacity(), size);
        if (rc != 0)
        {
            return fail(rc);
        }

        ++packet_;
        last_ = (size < capacity());
        pointData<mailbox::Header>(pdu.data())->len = static_cast<uint16_t>(sizeof(FoE::Header) + size);

        reply(std::move(pdu));
        return ProcessingResult::FINALIZE_AND_KEEP;
    }


    ProcessingResult FoEMessage::fail(uint32_t code)
    {
        endTransfer();
        reply(createPDU(FoE::opcode::ERROR, code, 0));
        return ProcessingResult::FINALIZE;
    }


    void FoEMessage::endTransfer()
    {
        // A writer released without a successful commit discards the transfer
        reader_.reset();
        writer_.reset();
    }


    std::vector<uint8_t> FoEMessage::createPDU(uint8_t opcode, uint32_t value, uint16_t payload_size)
    {
        std::vector<uint8_t> pdu(mailbox_size_, 0);
        auto* header = pointData<mailbox::Header>(pdu.data());
        auto* foe    = pointData<FoE::Header>(header);
        header->len  = static_cast<uint16_t>(sizeof(FoE::Header) + payload_size);
        header->type = mailbox::Type::FoE;
        foe->opcode  = opcode;
        foe->value   = value;
        return pdu;
    }


    uint32_t FoEMessage::capacity() const
    {
        return static_cast<uint32_t>(mailbox_size_ - FoE::OVERHEAD);
    }
}
