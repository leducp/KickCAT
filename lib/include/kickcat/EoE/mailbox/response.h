#ifndef KICKCAT_EOE_MAILBOX_RESPONSE_H
#define KICKCAT_EOE_MAILBOX_RESPONSE_H

#include "kickcat/EoE/protocol.h"
#include "kickcat/Mailbox.h"

namespace kickcat::mailbox::response
{
    std::shared_ptr<AbstractMessage> createEoEMessage(Mailbox* mbx, std::vector<uint8_t>&& raw_message);

    // Slave-side handler: parameter requests are answered via SlaveConfig; frame fragments go to
    // the mailbox reassembler and are consumed without a reply.
    class EoEMessage final : public AbstractMessage
    {
    public:
        EoEMessage(Mailbox* mbx, std::vector<uint8_t>&& raw_message);
        virtual ~EoEMessage() = default;

        ProcessingResult process() override;
        ProcessingResult process(std::vector<uint8_t> const& raw_message) override;

    private:
        ProcessingResult setIpParameter();
        ProcessingResult getIpParameter();
        ProcessingResult setAddressFilter();
        void replyResult(uint8_t frame_type, uint16_t result);

        mailbox::Header* header_;
        EoE::Header* eoe_;
    };
}

#endif
