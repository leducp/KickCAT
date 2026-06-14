#ifndef KICKCAT_EOE_MAILBOX_REQUEST_H
#define KICKCAT_EOE_MAILBOX_REQUEST_H

#include "kickcat/Mailbox.h"
#include "kickcat/EoE/protocol.h"

namespace kickcat::mailbox::request
{
    // Set IP parameters on a slave (ETG.1000.6 Set IP Parameter request). Awaits the slave's result.
    // On a non-success result, status() is (MessageStatus::EOE_RESULT | <ETG.1000.6 result code>).
    class SetIPMessage final : public AbstractMessage
    {
    public:
        SetIPMessage(uint16_t mailbox_size, EoE::IpParameters const& params, nanoseconds timeout);
        virtual ~SetIPMessage() = default;

        ProcessingResult process(uint8_t const* received) override;
    };

    // Read the IP parameters of a slave. On success fills the caller-supplied IpParameters.
    class GetIPMessage final : public AbstractMessage
    {
    public:
        GetIPMessage(uint16_t mailbox_size, EoE::IpParameters* result, nanoseconds timeout);
        virtual ~GetIPMessage() = default;

        ProcessingResult process(uint8_t const* received) override;

    private:
        EoE::IpParameters* client_params_;
    };

    // A single, already-built EoE fragment. Unacknowledged: written once, never awaits a reply.
    class FrameFragmentMessage final : public AbstractMessage
    {
    public:
        FrameFragmentMessage(uint16_t mailbox_size, EoE::Header const& header, uint8_t const* data, size_t data_len);
        virtual ~FrameFragmentMessage() = default;

        ProcessingResult process(uint8_t const* received) override;
    };

    // Persistent listener reassembling inbound tunneled frames into mailbox.eoe_frames.
    class FrameListenerMessage final : public AbstractMessage
    {
    public:
        FrameListenerMessage(Mailbox& mailbox);
        virtual ~FrameListenerMessage() = default;

        ProcessingResult process(uint8_t const* received) override;

    private:
        Mailbox& mailbox_;
        EoE::Reassembler reassembler_;
    };
}

#endif
