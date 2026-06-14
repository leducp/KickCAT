#ifndef KICKCAT_EOE_MAILBOX_RESPONSE_H
#define KICKCAT_EOE_MAILBOX_RESPONSE_H

#include "kickcat/Mailbox.h"
#include "kickcat/EoE/protocol.h"

namespace kickcat::mailbox::response
{
    std::shared_ptr<AbstractMessage> createEoEMessage(
            Mailbox* mbx,
            std::vector<uint8_t>&& raw_message);

    class SetIPMessage final : public AbstractMessage
    {
    public:
        SetIPMessage(Mailbox* mbx, std::vector<uint8_t>&& raw_message);
        virtual ~SetIPMessage() = default;

        ProcessingResult process() override;
        ProcessingResult process(std::vector<uint8_t> const& raw_message) override;
    };

    class GetIPMessage final : public AbstractMessage
    {
    public:
        GetIPMessage(Mailbox* mbx, std::vector<uint8_t>&& raw_message);
        virtual ~GetIPMessage() = default;

        ProcessingResult process() override;
        ProcessingResult process(std::vector<uint8_t> const& raw_message) override;
    };

    // Reassembles an inbound tunneled frame across its fragments, then delivers it to the mailbox.
    class FrameMessage final : public AbstractMessage
    {
    public:
        FrameMessage(Mailbox* mbx, std::vector<uint8_t>&& raw_message);
        virtual ~FrameMessage() = default;

        ProcessingResult process() override;
        ProcessingResult process(std::vector<uint8_t> const& raw_message) override;

    private:
        ProcessingResult addFragment(uint8_t const* raw_message);

        EoE::Reassembler reassembler_;
        bool first_consumed_{false}; // the initial fragment (in data_) is processed exactly once
    };
}

#endif
