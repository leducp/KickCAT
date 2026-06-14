#include <cstdint>
#include <cstring>

#include "Mailbox.h"
#include "protocol.h"
#include "kickcat/EoE/mailbox/response.h"

namespace kickcat::mailbox::response
{
    std::shared_ptr<AbstractMessage> createEoEMessage(Mailbox* mbx, std::vector<uint8_t>&& raw_message)
    {
        auto const* header = pointData<mailbox::Header>(raw_message.data());
        if (header->type != mailbox::Type::EoE)
        {
            return nullptr;
        }

        auto const* eoe = pointData<EoE::Header>(header);
        switch (eoe->type)
        {
            case EoE::request::SET_IP:  { return std::make_shared<SetIPMessage>(mbx, std::move(raw_message)); }
            case EoE::request::GET_IP:  { return std::make_shared<GetIPMessage>(mbx, std::move(raw_message)); }
            case EoE::FRAME_FRAGMENT:   { return std::make_shared<FrameMessage>(mbx, std::move(raw_message)); }
            default:                    { return nullptr; }
        }
    }

    namespace
    {
        std::vector<uint8_t> makeReply(uint8_t frame_type, uint16_t payload_len)
        {
            std::vector<uint8_t> reply(sizeof(mailbox::Header) + sizeof(EoE::Header) + payload_len, 0);
            auto* header = pointData<mailbox::Header>(reply.data());
            auto* eoe    = pointData<EoE::Header>(header);

            header->type = mailbox::Type::EoE;
            header->len  = static_cast<uint16_t>(sizeof(EoE::Header) + payload_len);

            eoe->type          = frame_type;
            eoe->last_fragment = 1;
            return reply;
        }
    }


    SetIPMessage::SetIPMessage(Mailbox* mbx, std::vector<uint8_t>&& raw_message)
        : AbstractMessage{mbx}
    {
        data_ = std::move(raw_message);
    }

    ProcessingResult SetIPMessage::process()
    {
        auto const* header = pointData<mailbox::Header>(data_.data());
        auto const* eoe    = pointData<EoE::Header>(header);

        uint16_t result = EoE::result::SUCCESS;
        EoE::IpParameters* params = mailbox_->eoeParameters();
        if (params == nullptr)
        {
            result = EoE::result::NO_IP_SUPPORT;
        }
        else if (header->len < sizeof(EoE::Header))
        {
            result = EoE::result::UNSPECIFIED_ERROR;
        }
        else
        {
            auto const* payload = pointData<uint8_t>(eoe);
            uint16_t param_len  = static_cast<uint16_t>(header->len - sizeof(EoE::Header));
            if (EoE::readIpParameters(payload, param_len, *params) == 0)
            {
                result = EoE::result::UNSPECIFIED_ERROR;
            }
        }

        // ETG.1000.6 Table 84 / Figure 36: the result is the EoE header's last word (TEOEPARAHEADER),
        // not a payload after the header. Mailbox length is 0x04.
        std::vector<uint8_t> reply_msg(sizeof(mailbox::Header) + sizeof(EoE::ParameterHeader), 0);
        auto* reply_header = pointData<mailbox::Header>(reply_msg.data());
        auto* reply_para   = pointData<EoE::ParameterHeader>(reply_header);
        reply_header->type        = mailbox::Type::EoE;
        reply_header->len         = sizeof(EoE::ParameterHeader);
        reply_para->type          = EoE::response::SET_IP;
        reply_para->last_fragment = 1;
        reply_para->result        = result;
        reply(std::move(reply_msg));
        return ProcessingResult::FINALIZE;
    }

    ProcessingResult SetIPMessage::process(std::vector<uint8_t> const&)
    {
        return ProcessingResult::NOOP;
    }


    GetIPMessage::GetIPMessage(Mailbox* mbx, std::vector<uint8_t>&& raw_message)
        : AbstractMessage{mbx}
    {
        data_ = std::move(raw_message);
    }

    ProcessingResult GetIPMessage::process()
    {
        EoE::IpParameters params{};
        EoE::IpParameters* stored = mailbox_->eoeParameters();
        if (stored != nullptr)
        {
            params = *stored;
        }

        // Worst case: every field present. Size the reply for that, then trim to what was written.
        constexpr uint16_t max_payload = sizeof(EoE::Parameter) + sizeof(EoE::MAC) + sizeof(EoE::IP) * 4
                                       + sizeof(EoE::DNS_NAME);
        auto reply_msg = makeReply(EoE::response::GET_IP, max_payload);

        auto* header  = pointData<mailbox::Header>(reply_msg.data());
        auto* eoe     = pointData<EoE::Header>(header);
        auto* payload = pointData<uint8_t>(eoe);
        uint16_t written = EoE::writeIpParameters(payload, max_payload, params);

        header->len = static_cast<uint16_t>(sizeof(EoE::Header) + written);
        reply_msg.resize(sizeof(mailbox::Header) + header->len);
        reply(std::move(reply_msg));
        return ProcessingResult::FINALIZE;
    }

    ProcessingResult GetIPMessage::process(std::vector<uint8_t> const&)
    {
        return ProcessingResult::NOOP;
    }


    FrameMessage::FrameMessage(Mailbox* mbx, std::vector<uint8_t>&& raw_message)
        : AbstractMessage{mbx}
    {
        data_ = std::move(raw_message);
    }

    ProcessingResult FrameMessage::addFragment(uint8_t const* raw_message)
    {
        auto const* header = pointData<mailbox::Header>(raw_message);
        auto const* eoe    = pointData<EoE::Header>(header);
        if (header->len < sizeof(EoE::Header))
        {
            return ProcessingResult::FINALIZE;
        }

        auto const* data = pointData<uint8_t>(eoe);
        size_t data_len  = header->len - sizeof(EoE::Header);
        if (reassembler_.add(eoe, data, data_len))
        {
            mailbox_->deliverEoEFrame(reassembler_.frame().data(), reassembler_.frame().size(), eoe->port);
            return ProcessingResult::FINALIZE;
        }
        return ProcessingResult::FINALIZE_AND_KEEP;
    }

    ProcessingResult FrameMessage::process()
    {
        // process() is re-invoked every cycle; the initial fragment must be consumed only once.
        if (first_consumed_)
        {
            return ProcessingResult::NOOP;
        }
        first_consumed_ = true;
        return addFragment(data_.data());
    }

    ProcessingResult FrameMessage::process(std::vector<uint8_t> const& raw_message)
    {
        auto const* header = pointData<mailbox::Header>(raw_message.data());
        if (header->type != mailbox::Type::EoE)
        {
            return ProcessingResult::NOOP;
        }
        auto const* eoe = pointData<EoE::Header>(header);
        if (eoe->type != EoE::FRAME_FRAGMENT)
        {
            return ProcessingResult::NOOP;
        }
        return addFragment(raw_message.data());
    }
}
