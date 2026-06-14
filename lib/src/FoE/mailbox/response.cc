#include <cstring>

#include "Mailbox.h"
#include "protocol.h"
#include "kickcat/FoE/mailbox/response.h"

namespace kickcat::mailbox::response
{
    uint32_t InMemoryFileSystem::read(std::string const& name, uint32_t, std::vector<uint8_t>& out)
    {
        auto it = files_.find(name);
        if (it == files_.end())
        {
            return FoE::result::NOT_FOUND;
        }
        out = it->second;
        return 0;
    }

    uint32_t InMemoryFileSystem::write(std::string const& name, uint32_t, std::vector<uint8_t> const& data)
    {
        files_[name] = data;
        return 0;
    }

    void InMemoryFileSystem::setFile(std::string const& name, std::vector<uint8_t> data)
    {
        files_[name] = std::move(data);
    }

    bool InMemoryFileSystem::hasFile(std::string const& name) const
    {
        return files_.find(name) != files_.end();
    }

    std::vector<uint8_t> const& InMemoryFileSystem::file(std::string const& name) const
    {
        return files_.at(name);
    }


    namespace
    {
        constexpr size_t DATA_OVERHEAD = sizeof(mailbox::Header) + sizeof(FoE::Header) + sizeof(FoE::data::Header);
    }

    std::shared_ptr<AbstractMessage> createFoEMessage(Mailbox* mbx, std::vector<uint8_t>&& raw_message)
    {
        auto const* header = pointData<mailbox::Header>(raw_message.data());
        if (header->type != mailbox::Type::FoE)
        {
            return nullptr;
        }

        auto const* foe = pointData<FoE::Header>(header);
        // Only a Read/Write Request starts a transfer; Data/Ack are routed to the existing message.
        if ((foe->opcode != FoE::opcode::READ) and (foe->opcode != FoE::opcode::WRITE))
        {
            return nullptr;
        }
        return std::make_shared<FoEMessage>(mbx, std::move(raw_message));
    }


    FoEMessage::FoEMessage(Mailbox* mbx, std::vector<uint8_t>&& raw_message)
        : AbstractMessage{mbx}
        , max_data_(static_cast<uint32_t>(mbx->maxMessageSize() - DATA_OVERHEAD))
    {
        data_ = std::move(raw_message);
    }

    ProcessingResult FoEMessage::process()
    {
        if (started_)
        {
            return ProcessingResult::NOOP; // re-invoked each cycle; the request is handled once
        }
        started_ = true;

        auto const* header = pointData<mailbox::Header>(data_.data());
        if ((sizeof(mailbox::Header) + header->len) > data_.size())
        {
            sendError(FoE::result::ILLEGAL); // declared length exceeds the received mailbox buffer
            return ProcessingResult::FINALIZE;
        }

        auto const* foe = pointData<FoE::Header>(header);
        if (foe->opcode == FoE::opcode::READ)
        {
            return startRead();
        }
        if (foe->opcode == FoE::opcode::WRITE)
        {
            return startWrite();
        }
        sendError(FoE::result::ILLEGAL);
        return ProcessingResult::FINALIZE;
    }

    ProcessingResult FoEMessage::process(std::vector<uint8_t> const& raw_message)
    {
        auto const* header = pointData<mailbox::Header>(raw_message.data());
        if (header->type != mailbox::Type::FoE)
        {
            return ProcessingResult::NOOP;
        }
        if ((sizeof(mailbox::Header) + header->len) > raw_message.size())
        {
            sendError(FoE::result::ILLEGAL); // declared length exceeds the received mailbox buffer
            return ProcessingResult::FINALIZE;
        }
        auto const* foe = pointData<FoE::Header>(header);

        if (writing_)
        {
            if (foe->opcode != FoE::opcode::DATA)
            {
                return ProcessingResult::NOOP;
            }
            return onData(foe, header->len);
        }

        if (foe->opcode != FoE::opcode::ACK)
        {
            return ProcessingResult::NOOP;
        }
        if (header->len < (sizeof(FoE::Header) + sizeof(FoE::ack::Header)))
        {
            sendError(FoE::result::PACKET_NUMBER_WRONG);
            return ProcessingResult::FINALIZE;
        }
        return onAck(pointData<FoE::ack::Header>(foe)->packet_number);
    }

    ProcessingResult FoEMessage::startRead()
    {
        auto const* header = pointData<mailbox::Header>(data_.data());
        auto const* foe    = pointData<FoE::Header>(header);
        if (header->len < (sizeof(FoE::Header) + sizeof(FoE::read::Header)))
        {
            sendError(FoE::result::ILLEGAL);
            return ProcessingResult::FINALIZE;
        }

        auto const* rd   = pointData<FoE::read::Header>(foe);
        auto const* name = pointData<uint8_t>(rd);
        uint16_t name_len = static_cast<uint16_t>(header->len - sizeof(FoE::Header) - sizeof(FoE::read::Header));
        file_name_.assign(reinterpret_cast<char const*>(name), name_len);
        password_ = rd->password;

        uint32_t err = mailbox_->fileSystem().read(file_name_, password_, buffer_);
        if (err != 0)
        {
            sendError(err);
            return ProcessingResult::FINALIZE;
        }
        return sendData();
    }

    ProcessingResult FoEMessage::startWrite()
    {
        auto const* header = pointData<mailbox::Header>(data_.data());
        auto const* foe    = pointData<FoE::Header>(header);
        if (header->len < (sizeof(FoE::Header) + sizeof(FoE::write::Header)))
        {
            sendError(FoE::result::ILLEGAL);
            return ProcessingResult::FINALIZE;
        }

        auto const* wr   = pointData<FoE::write::Header>(foe);
        auto const* name = pointData<uint8_t>(wr);
        uint16_t name_len = static_cast<uint16_t>(header->len - sizeof(FoE::Header) - sizeof(FoE::write::Header));
        file_name_.assign(reinterpret_cast<char const*>(name), name_len);
        password_ = wr->password;

        writing_ = true;
        sendAck(0);
        return ProcessingResult::FINALIZE_AND_KEEP;
    }

    ProcessingResult FoEMessage::sendData()
    {
        uint32_t remaining = static_cast<uint32_t>(buffer_.size()) - offset_;
        uint32_t chunk = remaining;
        if (chunk > max_data_)
        {
            chunk = max_data_;
        }

        std::vector<uint8_t> msg(DATA_OVERHEAD + chunk, 0);
        auto* header  = pointData<mailbox::Header>(msg.data());
        auto* foe     = pointData<FoE::Header>(header);
        auto* dh      = pointData<FoE::data::Header>(foe);
        auto* payload = pointData<uint8_t>(dh);

        header->type = mailbox::Type::FoE;
        foe->opcode  = FoE::opcode::DATA;
        packet_number_++;
        dh->packet_number = packet_number_;
        if (chunk > 0)
        {
            std::memcpy(payload, buffer_.data() + offset_, chunk);
        }
        offset_ += chunk;
        header->len = static_cast<uint16_t>(sizeof(FoE::Header) + sizeof(FoE::data::Header) + chunk);
        reply(std::move(msg));

        if (chunk < max_data_)
        {
            last_data_ = true; // a packet shorter than the max ends the transfer
        }
        return ProcessingResult::FINALIZE_AND_KEEP;
    }

    ProcessingResult FoEMessage::onAck(uint32_t packet_number)
    {
        if (packet_number != packet_number_)
        {
            sendError(FoE::result::PACKET_NUMBER_WRONG);
            return ProcessingResult::FINALIZE;
        }
        if (last_data_)
        {
            return ProcessingResult::FINALIZE;
        }
        return sendData();
    }

    ProcessingResult FoEMessage::onData(FoE::Header const* foe, uint16_t len)
    {
        if (len < (sizeof(FoE::Header) + sizeof(FoE::data::Header)))
        {
            sendError(FoE::result::ILLEGAL);
            return ProcessingResult::FINALIZE;
        }

        auto const* dh      = pointData<FoE::data::Header>(foe);
        auto const* payload = pointData<uint8_t>(dh);
        uint32_t data_len   = static_cast<uint32_t>(len - sizeof(FoE::Header) - sizeof(FoE::data::Header));

        if (dh->packet_number != expected_packet_)
        {
            sendError(FoE::result::PACKET_NUMBER_WRONG);
            return ProcessingResult::FINALIZE;
        }
        buffer_.insert(buffer_.end(), payload, payload + data_len);

        bool last = (data_len < max_data_);
        if (last)
        {
            uint32_t err = mailbox_->fileSystem().write(file_name_, password_, buffer_);
            if (err != 0)
            {
                sendError(err);
                return ProcessingResult::FINALIZE;
            }
            sendAck(dh->packet_number);
            return ProcessingResult::FINALIZE;
        }
        sendAck(dh->packet_number);
        expected_packet_++;
        return ProcessingResult::FINALIZE_AND_KEEP;
    }

    void FoEMessage::sendError(uint32_t code)
    {
        std::vector<uint8_t> msg(sizeof(mailbox::Header) + sizeof(FoE::Header) + sizeof(FoE::error::Header), 0);
        auto* header = pointData<mailbox::Header>(msg.data());
        auto* foe    = pointData<FoE::Header>(header);
        auto* eh     = pointData<FoE::error::Header>(foe);
        header->type   = mailbox::Type::FoE;
        header->len    = sizeof(FoE::Header) + sizeof(FoE::error::Header);
        foe->opcode    = FoE::opcode::ERROR;
        eh->error_code = code;
        reply(std::move(msg));
    }

    void FoEMessage::sendAck(uint32_t packet_number)
    {
        std::vector<uint8_t> msg(sizeof(mailbox::Header) + sizeof(FoE::Header) + sizeof(FoE::ack::Header), 0);
        auto* header = pointData<mailbox::Header>(msg.data());
        auto* foe    = pointData<FoE::Header>(header);
        auto* ah     = pointData<FoE::ack::Header>(foe);
        header->type      = mailbox::Type::FoE;
        header->len       = sizeof(FoE::Header) + sizeof(FoE::ack::Header);
        foe->opcode       = FoE::opcode::ACK;
        ah->packet_number = packet_number;
        reply(std::move(msg));
    }
}
