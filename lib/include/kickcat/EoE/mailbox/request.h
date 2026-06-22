#ifndef KICKCAT_EOE_MAILBOX_REQUEST_H
#define KICKCAT_EOE_MAILBOX_REQUEST_H

#include "kickcat/EoE/FrameReassembler.h"
#include "kickcat/EoE/protocol.h"
#include "kickcat/Mailbox.h"

namespace kickcat::mailbox::request
{
    uint32_t eoeResultToStatus(uint16_t result);   // ETG.1000.6 Table 85

    class SetIPParameterMessage final : public AbstractMessage
    {
    public:
        SetIPParameterMessage(uint16_t mailbox_size, EoE::IpParameters const& params, nanoseconds timeout);
        virtual ~SetIPParameterMessage() = default;

        ProcessingResult process(uint8_t const* received) override;

    private:
        EoE::Header* eoe_;
    };

    class GetIPParameterMessage final : public AbstractMessage
    {
    public:
        GetIPParameterMessage(uint16_t mailbox_size, EoE::IpParameters* params, nanoseconds timeout);
        virtual ~GetIPParameterMessage() = default;

        ProcessingResult process(uint8_t const* received) override;

    private:
        EoE::Header* eoe_;
        EoE::IpParameters* client_params_;
    };

    class SetAddressFilterMessage final : public AbstractMessage
    {
    public:
        SetAddressFilterMessage(uint16_t mailbox_size, EoE::AddressFilter const& filter, nanoseconds timeout);
        virtual ~SetAddressFilterMessage() = default;

        ProcessingResult process(uint8_t const* received) override;

    private:
        EoE::Header* eoe_;
    };

    // Resident handler for unsolicited inbound frames (like the CoE EmergencyMessage): it never
    // finalizes (FINALIZE_AND_KEEP) so it stays in to_process to catch each frame.
    class EoEReceiveMessage final : public AbstractMessage
    {
    public:
        EoEReceiveMessage(uint16_t mailbox_size);
        virtual ~EoEReceiveMessage() = default;

        void setFrameSink(EoE::FrameSink sink) { sink_ = std::move(sink); }

        ProcessingResult process(uint8_t const* received) override;

    private:
        EoE::FrameReassembler reassembler_;
        EoE::FrameSink sink_;
    };

    // One outbound frame fragment. Starts in MessageStatus::SUCCESS so send() writes it but never
    // adds it to to_process (no reply is expected).
    class EoEFrameMessage final : public AbstractMessage
    {
    public:
        EoEFrameMessage(uint16_t mailbox_size, std::vector<uint8_t>&& fragment);
        virtual ~EoEFrameMessage() = default;

        ProcessingResult process(uint8_t const* received) override;
    };
}

#endif
