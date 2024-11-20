#ifndef KICKCAT_MAILBOX_H
#define KICKCAT_MAILBOX_H

#include <queue>
#include <list>
#include <memory>
#include <functional>

#include "kickcat/protocol.h"
#include "kickcat/AbstractESC.h"
#include "CoE/OD.h"


namespace kickcat::mailbox
{
    enum class ProcessingResult
    {
        NOOP,
        CONTINUE,
        FINALIZE,
        FINALIZE_AND_KEEP
    };
}

namespace kickcat::mailbox::request
{
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

    /// \brief Request mailbox - it orchestrates the emission and the processing of messages (for master)
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
}

namespace kickcat::mailbox::response
{
    class Mailbox;

    class AbstractMessage
    {
    public:
        AbstractMessage(Mailbox* mbx);
        virtual ~AbstractMessage() = default;

        /// \brief Process the message
        virtual ProcessingResult process() = 0;

        /// \brief process() variant for state message (FoE, SDO segmented transfer)
        /// \param raw_message  A raw message that may be processed by this message
        virtual ProcessingResult process(std::vector<uint8_t> const& raw_message) = 0;

        uint8_t const* data() const { return data_.data(); }
        size_t size() const         { return data_.size(); }

    
    protected:
        void reply(std::vector<uint8_t>&& reply); /// Enqueue a raw message to be sent in the mailbox
        void replyError(std::vector<uint8_t>&& raw_message, uint16_t code); // wrapper on mailbox replyError
        uint16_t replyExpectedSize();

        std::vector<uint8_t> data_;
        Mailbox* mailbox_;
    };

    /// \brief Response mailbox - it orchestrates the reception and the processing of messages (for slaves and gateway)
    class Mailbox
    {
        friend class AbstractMessage;
    public:
        /// \param max_msgs Max messages allowed simultaneously in the processing queue
        Mailbox(AbstractESC* esc, SyncManagerConfig mbx_in, SyncManagerConfig mbx_out, uint16_t max_msgs = 1);
        ~Mailbox() = default;

        void enableCoE(CoE::Dictionary&& dictionary);
        CoE::Dictionary& getDictionary(){return dictionary_;}

        void receive(); // Try to receive a message from the ESC
        void process(); // Process a message in the to_process_ queue if any
        void send();    // Send a message in the to_send_ queue if any, keep it in the queue if the ESC is not ready yet

        void replyError(std::vector<uint8_t>&& raw_message, uint16_t code);

        AbstractESC* esc_;
        SyncManagerConfig mbx_in_;
        SyncManagerConfig mbx_out_;
        uint16_t max_msgs_;

        // session handle, from 1 to 7, it is used to detect duplicate frame
        uint8_t counter_{0};

        std::vector<std::function<std::shared_ptr<AbstractMessage>(Mailbox*, std::vector<uint8_t>&&)>> factories_;

        std::list<std::shared_ptr<AbstractMessage>>  to_process_;   /// Received messages, waiting to be processed
        std::queue<std::vector<uint8_t>> to_send_;      /// Messages to send (replies from a received messages)
        CoE::Dictionary dictionary_;
    };
}

#endif
