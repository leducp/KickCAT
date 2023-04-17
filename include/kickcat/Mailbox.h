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
        CONTINUE,
        FINALIZE,
        FINALIZE_AND_KEEP
    };

    namespace MessageStatus
    {
        constexpr uint32_t SUCCESS                      = 0x000;
        constexpr uint32_t RUNNING                      = 0x001;
        constexpr uint32_t TIMEDOUT                     = 0x002;

        constexpr uint32_t COE_WRONG_SERVICE            = 0x101;
        constexpr uint32_t COE_UNKNOWN_SERVICE          = 0x102;
        constexpr uint32_t COE_CLIENT_BUFFER_TOO_SMALL  = 0x103;
        constexpr uint32_t COE_SEGMENT_BAD_TOGGLE_BIT   = 0x103;
    }

    class AbstractMessage
    {
    public:
        /// \param mailbox_size Size of the mailbox the message is targeted to (required to adapt internal buffer)
        AbstractMessage(uint16_t mailbox_size, nanoseconds timeout);
        virtual ~AbstractMessage() = default;

        // set message counter (aka session handle)
        void setCounter(uint8_t counter) { header_->count = counter & 0x7; }

        /// \brief try to process the payload
        /// \return NOOP if the received message is not related to this one
        /// \return FINALIZE if the message is related and operation is finished.
        /// \return CONTINUE if the message is related and operation requiered another loop (message shall be push again in sending queue)
        virtual ProcessingResult process(uint8_t const* received) = 0;

        /// Handle address field. Address meaning depends on context (0 for local processing, slave address for gateway processing)
        void setAddress(uint16_t address) { header_->address = address; }
        uint16_t address() const { return header_->address; }

        /// \brief Message status
        /// \param current_time     Considered time to compute message status (enable injection for tests)
        /// \return current message status. Value may depend on underlying service.
        uint32_t status(nanoseconds current_time = since_epoch());

        uint8_t const* data() const { return data_.data(); }
        size_t size() const         { return data_.size(); }

    protected:
        std::vector<uint8_t> data_;     // data of the message (send and gateway rec)
        mailbox::Header* header_;       // pointer on the mailbox header in data
        uint32_t status_;               // message current status

    private:
        nanoseconds timeout_;           // Max time to handle the message. Relative time before sending, absolute time after. 0 means no timeout
    };


    /// \brief Handle message that are processed through a Mailbox gateway (cf. ETG.8200)
    /// \details Gateway message are standard mailbox messages that are not processed locally
    class GatewayMessage : public AbstractMessage
    {
    public:
        GatewayMessage(uint16_t mailbox_size, uint8_t const* raw_message, uint16_t gateway_index, nanoseconds timeout);
        virtual ~GatewayMessage() = default;

        ProcessingResult process(uint8_t const* received) override;
        uint16_t gatewayIndex() const { return gateway_index_; }

    private:
        uint16_t address_; // Requested address: store it to set it back after processing because the header field is used to store gateway index
        uint16_t gateway_index_;
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

        //
        void generateSMConfig(SyncManager SM[2]);

        // messages factory
        std::shared_ptr<AbstractMessage> createSDO(uint16_t index, uint8_t subindex, bool CA, uint8_t request, void* data, uint32_t* data_size, nanoseconds timeout = 20ms);
        std::shared_ptr<GatewayMessage>  createGatewayMessage(uint8_t const* raw_message, int32_t raw_message_size, uint16_t gateway_index, nanoseconds timeout = 20ms);

        // helper to get next message to send and transfer it to reception callbacks if required
        std::shared_ptr<AbstractMessage> send();

        /// \brief Receive a message
        /// \param raw_message      Raw message read on the bus
        /// \param current_time     Considered time to process message timeout (enable injection for tests)
        bool receive(uint8_t const* raw_message, nanoseconds current_time = since_epoch());


        std::queue<std::shared_ptr<AbstractMessage>> to_send;     // message waiting to be sent
        std::list <std::shared_ptr<AbstractMessage>> to_process;  // message already sent, waiting for an answer

        uint8_t nextCounter();

        std::vector<CoE::Emergency> emergencies;
    private:
    };

    class SDOMessage : public AbstractMessage
    {
    public:
        SDOMessage(uint16_t mailbox_size, uint16_t index, uint8_t subindex, bool CA, uint8_t request, void* data, uint32_t* data_size, nanoseconds timeout);
        virtual ~SDOMessage() = default;

        ProcessingResult process(uint8_t const* received) override;

    protected:
        ProcessingResult processUpload           (mailbox::Header const* header, CoE::ServiceData const* sdo, uint8_t const* payload);
        ProcessingResult processUploadSegmented  (mailbox::Header const* header, CoE::ServiceData const* sdo, uint8_t const* payload);
        ProcessingResult processDownload         (mailbox::Header const* header, CoE::ServiceData const* sdo, uint8_t const* payload);
        ProcessingResult processDownloadSegmented(mailbox::Header const* header, CoE::ServiceData const* sdo, uint8_t const* payload);

        CoE::Header* coe_;
        CoE::ServiceData* sdo_;
        uint8_t* payload_;
        uint8_t* client_data_;
        uint32_t* client_data_size_;
    };

    class EmergencyMessage : public AbstractMessage
    {
    public:
        EmergencyMessage(Mailbox& mailbox);
        virtual ~EmergencyMessage() = default;

        ProcessingResult process(uint8_t const* received) override;

    private:
        Mailbox& mailbox_;
    };
}

#endif
