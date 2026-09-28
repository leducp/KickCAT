#ifndef KICKCAT_FOE_MAILBOX_REQUEST_H
#define KICKCAT_FOE_MAILBOX_REQUEST_H

#include "kickcat/Mailbox.h"
#include "kickcat/FoE/protocol.h"

namespace kickcat::mailbox::request
{
    /// \brief FoE client side of a file transfer (ETG.1000.6 chapter 5.8)
    /// \details The status is SUCCESS, RUNNING, TIMEDOUT, a MessageStatus::FOE_* code or the (normalized) FoE error
    ///          code sent by the slave. The timeout applies to each exchange, not to the whole transfer.
    class FoEMessage final : public AbstractMessage
    {
    public:
        /// \param opcode   FoE::opcode::READ or FoE::opcode::WRITE
        /// \param file     Content to write, ignored for a read
        FoEMessage(uint16_t mbx_recv_size, uint16_t mbx_send_size, uint8_t opcode, std::string const& name,
                   uint32_t password, std::vector<uint8_t> file, nanoseconds timeout);
        virtual ~FoEMessage() = default;

        ProcessingResult process(uint8_t const* received) override;
        void sent() override;

        /// Read: content received so far. Write: content to send.
        std::vector<uint8_t>& file() { return file_; }

        /// Read: bytes received. Write: bytes acknowledged by the slave.
        uint32_t bytesTransferred() const;

        /// Optional text sent by the slave along with its error code
        std::string const& errorText() const { return error_text_; }

        /// \brief Abort the transfer: the next PDU sent is an FoE error, then the status becomes FOE_CANCELLED.
        /// \details The mailbox shall keep being processed until the status leaves RUNNING.
        void cancel() { cancelled_ = true; }

    private:
        ProcessingResult processRead (uint8_t opcode, uint32_t value, uint8_t const* payload, uint16_t payload_size);
        ProcessingResult processWrite(uint8_t opcode, uint32_t value);

        /// Send an FoE error to the slave, then end the message with the given status
        ProcessingResult abort(uint32_t code, uint32_t status);

        void prepare(uint8_t opcode, uint32_t value, uint16_t payload_size);
        void prepareData();

        FoE::Header* foe_;
        uint8_t* payload_;

        bool reading_;
        std::vector<uint8_t> file_;
        uint32_t offset_{0};        // write: bytes already put in a data packet
        uint32_t acknowledged_{0};  // write: bytes acknowledged by the slave
        uint32_t packet_{0};        // last packet number received (read) or sent (write)
        bool last_{false};          // write: the last data packet has been sent
        uint32_t pending_status_{MessageStatus::RUNNING}; // status to apply once the prepared PDU is sent
        bool cancelled_{false};
        std::string error_text_;
    };
}

#endif
