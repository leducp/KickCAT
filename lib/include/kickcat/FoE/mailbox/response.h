#ifndef KICKCAT_FOE_MAILBOX_RESPONSE_H
#define KICKCAT_FOE_MAILBOX_RESPONSE_H

#include "kickcat/Mailbox.h"
#include "kickcat/FoE/Storage.h"
#include "kickcat/FoE/protocol.h"

namespace kickcat::mailbox::response
{
    std::shared_ptr<AbstractMessage> createFoEMessage(Mailbox *mbx, std::vector<uint8_t>&& raw_message);

    /// \brief FoE server side of a file transfer (ETG.1000.6 chapter 5.8), backed by the mailbox storage
    class FoEMessage final : public AbstractMessage
    {
    public:
        FoEMessage(Mailbox* mbx, std::vector<uint8_t>&& raw_message);
        virtual ~FoEMessage() = default;

        ProcessingResult process() override;
        ProcessingResult process(std::vector<uint8_t> const& raw_message) override;

    private:
        ProcessingResult start(std::vector<uint8_t> const& raw_message);
        ProcessingResult readSession (uint8_t opcode, uint32_t value);
        ProcessingResult writeSession(uint8_t opcode, uint32_t value, uint8_t const* payload, uint16_t payload_size);
        ProcessingResult sendData();

        /// Abort the current transfer, if any, and send an FoE error to the master
        ProcessingResult fail(uint32_t code);
        void endTransfer();

        std::vector<uint8_t> createPDU(uint8_t opcode, uint32_t value, uint16_t payload_size);
        uint32_t capacity() const;

        FoE::AbstractStorage& storage_;
        std::unique_ptr<FoE::AbstractReader> reader_;   // read in progress
        std::unique_ptr<FoE::AbstractWriter> writer_;   // write in progress
        uint32_t packet_{0};            // last packet number sent (read) or received (write)
        bool last_{false};              // read: the last data packet has been sent
        std::size_t mailbox_size_{0};
    };
}

#endif
