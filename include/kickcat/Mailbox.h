#ifndef KICKCAT_MAILBOX_H
#define KICKCAT_MAILBOX_H

#include <queue>
#include <list>
#include <memory>

#include "protocol.h"

namespace kickcat
{
    enum class ProcessingResult
    {
        NOOP,
        FINALIZE,
        CONTINUE
    };

    namespace MessageStatus
    {
        constexpr uint32_t SUCCESS                      = 0x000;
        constexpr uint32_t RUNNING                      = 0x001;

        constexpr uint32_t COE_WRONG_SERVICE            = 0x101;
        constexpr uint32_t COE_UNKNOWN_SERVICE          = 0x102;
        constexpr uint32_t COE_CLIENT_BUFFER_TOO_SMALL  = 0x103;
        constexpr uint32_t COE_SEGMENT_BAD_TOGGLE_BIT   = 0x103;
    }

    class AbstractMessage
    {
    public:
        AbstractMessage(uint16_t mailbox_size);
        virtual ~AbstractMessage() = default;

        // set message counter (aka session handle)
        void setCounter(uint8_t counter) { header_->count = counter & 0x7; }

        /// \return  true if the message need an acknowledge, false otherwise
        virtual bool needAcknowledge() const = 0;

        /// \brief try to process the payload
        /// \return NOOP if the received message is not related to this one
        /// \return FINALIZE if the message is related and operation is finished.
        /// \return CONTINUE if the message is related and operation requiered another loop (message shall be push again in sending queue)
        virtual ProcessingResult process(uint8_t const* received) = 0;

        /// \brief Message status
        /// \return current message status. Value may depend on underlying service.
        uint32_t status() const { return status_; };

        std::vector<uint8_t> const& data() const { return data_; }

    protected:
        std::vector<uint8_t> data_;     // data of the message (send only)
        mailbox::Header* header_;       // pointer on the mailbox header in data
        uint32_t status_;               // message current status
    };

    class SDOMessage : public AbstractMessage
    {
    public:
        SDOMessage(uint16_t mailbox_size, uint16_t index, uint8_t subindex, bool CA, uint8_t request, void* data, uint32_t* data_size);
        virtual ~SDOMessage() = default;

        bool needAcknowledge() const override { return true; } // SDO service message always need to be ack
        ProcessingResult process(uint8_t const* received) override;

    protected:
        ProcessingResult processUpload           (mailbox::Header const* header, mailbox::ServiceData const* coe, uint8_t const* payload);
        ProcessingResult processUploadSegmented  (mailbox::Header const* header, mailbox::ServiceData const* coe, uint8_t const* payload);
        ProcessingResult processDownload         (mailbox::Header const* header, mailbox::ServiceData const* coe, uint8_t const* payload);
        ProcessingResult processDownloadSegmented(mailbox::Header const* header, mailbox::ServiceData const* coe, uint8_t const* payload);

        mailbox::ServiceData* coe_;
        uint8_t* payload_;
        uint8_t* client_data_;
        uint32_t* client_data_size_;
    };

    struct Mailbox
    {
        uint16_t recv_offset;
        uint16_t recv_size;
        uint16_t send_offset;
        uint16_t send_size;

        bool can_read;      // data available on the slave
        bool can_write;     // free space for a new message on the slave
        uint8_t counter{0}; // session handle, from 1 to 7
        bool toggle;        // for SDO segmented transfer

        std::shared_ptr<AbstractMessage> createSDO(uint16_t index, uint8_t subindex, bool CA, uint8_t request, void* data, uint32_t* data_size);

        bool receive(uint8_t const* raw_message);
        std::queue<std::shared_ptr<AbstractMessage>> to_send;     // message waiting to be sent
        std::list <std::shared_ptr<AbstractMessage>> to_process;  // message already sent, waiting for an answer

        uint8_t nextCounter();
    private:
    };
}

#endif
