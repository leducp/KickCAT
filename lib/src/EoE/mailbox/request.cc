#include <cstring>

#include "debug.h"
#include "Error.h"
#include "kickcat/EoE/mailbox/request.h"

namespace kickcat::mailbox::request
{
    namespace
    {
        void initEoEHeader(mailbox::Header* header, EoE::Header* eoe, uint8_t frame_type)
        {
            header->priority = 0;
            header->channel  = 0;
            header->reserved = 0;
            header->type     = mailbox::Type::EoE;

            eoe->type            = frame_type;
            eoe->port            = 0;
            eoe->last_fragment   = 1;
            eoe->time_appended   = 0;
            eoe->time_request    = 0;
            eoe->reserved        = 0;
            eoe->fragment_number = 0;
            eoe->offset          = 0;
            eoe->frame_number    = 0;
        }
    }

    SetIPMessage::SetIPMessage(uint16_t mailbox_size, EoE::IpParameters const& params, nanoseconds timeout)
        : AbstractMessage(mailbox_size, timeout)
    {
        auto* eoe     = pointData<EoE::Header>(header_);
        auto* payload = pointData<uint8_t>(eoe);

        initEoEHeader(header_, eoe, EoE::request::SET_IP);
        // guard the subtraction: a too-small buffer must not underflow into a huge "capacity"
        constexpr size_t overhead = sizeof(mailbox::Header) + sizeof(EoE::Header);
        size_t capacity = 0;
        if (data_.size() > overhead)
        {
            capacity = data_.size() - overhead;
        }
        uint16_t param_len = EoE::writeIpParameters(payload, capacity, params);
        header_->len = static_cast<uint16_t>(sizeof(EoE::Header) + param_len);
    }

    ProcessingResult SetIPMessage::process(uint8_t const* received)
    {
        auto const* header = pointData<mailbox::Header>(received);

        if ((header->address & mailbox::GATEWAY_MESSAGE_MASK) != 0)
        {
            return ProcessingResult::NOOP;
        }
        if (header->type != mailbox::Type::EoE)
        {
            return ProcessingResult::NOOP;
        }

        auto const* eoe = pointData<EoE::Header>(header);
        if (eoe->type != EoE::response::SET_IP)
        {
            return ProcessingResult::NOOP;
        }

        // ETG.1000.6 Table 84: the result lives in the EoE header's last word (TEOEPARAHEADER)
        if (header->len < sizeof(EoE::ParameterHeader))
        {
            status_ = MessageStatus::EOE_WRONG_SERVICE;
            return ProcessingResult::FINALIZE;
        }

        auto const* para = pointData<EoE::ParameterHeader>(header);

        status_ = MessageStatus::SUCCESS;
        if (para->result != EoE::result::SUCCESS)
        {
            // surface the ETG.1000.6 result code in status_ (low 16 bits), CoE-style
            status_ = MessageStatus::EOE_RESULT | para->result;
        }
        return ProcessingResult::FINALIZE;
    }


    GetIPMessage::GetIPMessage(uint16_t mailbox_size, EoE::IpParameters* result, nanoseconds timeout)
        : AbstractMessage(mailbox_size, timeout)
        , client_params_{result}
    {
        auto* eoe = pointData<EoE::Header>(header_);
        initEoEHeader(header_, eoe, EoE::request::GET_IP);
        header_->len = sizeof(EoE::Header);
    }

    ProcessingResult GetIPMessage::process(uint8_t const* received)
    {
        auto const* header = pointData<mailbox::Header>(received);

        if ((header->address & mailbox::GATEWAY_MESSAGE_MASK) != 0)
        {
            return ProcessingResult::NOOP;
        }
        if (header->type != mailbox::Type::EoE)
        {
            return ProcessingResult::NOOP;
        }

        auto const* eoe = pointData<EoE::Header>(header);
        if (eoe->type != EoE::response::GET_IP)
        {
            return ProcessingResult::NOOP;
        }

        if ((client_params_ == nullptr) or (header->len < sizeof(EoE::Header)))
        {
            status_ = MessageStatus::EOE_WRONG_SERVICE;
            return ProcessingResult::FINALIZE;
        }

        auto const* payload = pointData<uint8_t>(eoe);
        uint16_t param_len  = static_cast<uint16_t>(header->len - sizeof(EoE::Header));
        if (EoE::readIpParameters(payload, param_len, *client_params_) == 0)
        {
            status_ = MessageStatus::EOE_WRONG_SERVICE;
            return ProcessingResult::FINALIZE;
        }

        status_ = MessageStatus::SUCCESS;
        return ProcessingResult::FINALIZE;
    }


    FrameFragmentMessage::FrameFragmentMessage(uint16_t mailbox_size, EoE::Header const& header, uint8_t const* data, size_t data_len)
        : AbstractMessage(mailbox_size, 0ns)
    {
        auto* eoe     = pointData<EoE::Header>(header_);
        auto* payload = pointData<uint8_t>(eoe);

        header_->priority = 0;
        header_->channel  = 0;
        header_->reserved = 0;
        header_->type     = mailbox::Type::EoE;
        *eoe = header;
        std::memcpy(payload, data, data_len);
        header_->len = static_cast<uint16_t>(sizeof(EoE::Header) + data_len);

        status_ = MessageStatus::SUCCESS; // fire-and-forget: never enters the processing queue
    }

    ProcessingResult FrameFragmentMessage::process(uint8_t const*)
    {
        return ProcessingResult::NOOP;
    }


    FrameListenerMessage::FrameListenerMessage(Mailbox& mailbox)
        : AbstractMessage(mailbox.recv_size, 0ns)
        , mailbox_{mailbox}
    { }

    ProcessingResult FrameListenerMessage::process(uint8_t const* received)
    {
        auto const* header = pointData<mailbox::Header>(received);

        // gateway replies are owned by their GatewayMessage, not by this local listener
        if ((header->address & mailbox::GATEWAY_MESSAGE_MASK) != 0)
        {
            return ProcessingResult::NOOP;
        }
        if (header->type != mailbox::Type::EoE)
        {
            return ProcessingResult::NOOP;
        }

        auto const* eoe = pointData<EoE::Header>(header);
        if (eoe->type != EoE::FRAME_FRAGMENT)
        {
            return ProcessingResult::NOOP;
        }

        if (header->len < sizeof(EoE::Header))
        {
            return ProcessingResult::NOOP;
        }

        auto const* data = pointData<uint8_t>(eoe);
        size_t data_len  = header->len - sizeof(EoE::Header);
        if (reassembler_.add(eoe, data, data_len))
        {
            if (mailbox_.eoe_frames.size() >= MAX_BUFFERED_MESSAGES)
            {
                mailbox_.eoe_frames.erase(mailbox_.eoe_frames.begin());
            }
            mailbox_.eoe_frames.push_back(reassembler_.frame());
        }
        return ProcessingResult::FINALIZE_AND_KEEP;
    }


    std::shared_ptr<AbstractMessage> Mailbox::createEoESetIP(EoE::IpParameters const& params, nanoseconds timeout)
    {
        if (recv_size == 0)
        {
            THROW_ERROR("This mailbox is inactive");
        }
        if (recv_size < (sizeof(mailbox::Header) + sizeof(EoE::Header) + EoE::ipParametersSize(params)))
        {
            THROW_ERROR("Mailbox too small for the requested EoE IP parameters");
        }
        auto msg = std::make_shared<SetIPMessage>(recv_size, params, timeout);
        msg->setCounter(nextCounter());
        to_send.push(msg);
        return msg;
    }

    std::shared_ptr<AbstractMessage> Mailbox::createEoEGetIP(EoE::IpParameters* result, nanoseconds timeout)
    {
        if (recv_size == 0)
        {
            THROW_ERROR("This mailbox is inactive");
        }
        if (recv_size < (sizeof(mailbox::Header) + sizeof(EoE::Header)))
        {
            THROW_ERROR("Mailbox too small for EoE");
        }
        auto msg = std::make_shared<GetIPMessage>(recv_size, result, timeout);
        msg->setCounter(nextCounter());
        to_send.push(msg);
        return msg;
    }

    size_t Mailbox::createEoESendFrame(uint8_t const* frame, size_t size, uint8_t port)
    {
        if (recv_size == 0)
        {
            THROW_ERROR("This mailbox is inactive");
        }

        constexpr size_t overhead = sizeof(mailbox::Header) + sizeof(EoE::Header);
        if (recv_size < (overhead + EoE::FRAGMENT_GRANULARITY))
        {
            THROW_ERROR("Mailbox too small for EoE frame tunneling");
        }
        if (size > EoE::MAX_FRAGMENTED_FRAME)
        {
            // the 6-bit offset/complete-size header fields cannot describe a larger frame
            THROW_ERROR("Frame too large for EoE tunneling");
        }

        uint8_t frame_number = eoe_frame_number & 0xf;
        eoe_frame_number++;

        size_t count = 0;
        EoE::Fragmenter fragmenter(frame, size, port, frame_number, recv_size - overhead);
        while (not fragmenter.done())
        {
            EoE::Header header{};
            uint8_t const* data = nullptr;
            size_t len = fragmenter.next(header, data);

            auto msg = std::make_shared<FrameFragmentMessage>(recv_size, header, data, len);
            msg->setCounter(nextCounter());
            to_send.push(msg);
            count++;
        }
        return count;
    }

    std::shared_ptr<AbstractMessage> Mailbox::createEoEFrameListener()
    {
        auto msg = std::make_shared<FrameListenerMessage>(*this);
        to_process.push_back(msg);
        return msg;
    }
}
