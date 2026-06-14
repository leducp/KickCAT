#ifndef KICKCAT_MAILBOX_H
#define KICKCAT_MAILBOX_H

#include <queue>
#include <list>
#include <memory>
#include <functional>
#include <string>

#include "kickcat/protocol.h"
#include "kickcat/AbstractESC.h"
#include "CoE/OD.h"
#include "CoE/protocol.h"
#include "EoE/protocol.h"
#include "FoE/protocol.h"


namespace kickcat
{
    class AbstractESC;
}

namespace kickcat::mailbox
{
    enum class ProcessingResult
    {
        NOOP,
        CONTINUE,
        FINALIZE,
        FINALIZE_AND_KEEP
    };

    // Helper to compute new counter - used as session handle
    // The input counter is increased is roleld from 1 to 7 by one increment and then returned
    uint8_t nextCounter(uint8_t& currentCounter);

    // Upper bound on the unsolicited buffers a consumer polls (CoE emergencies, EoE frames): the
    // oldest entry is dropped past this, so a consumer that never drains cannot grow them unbounded.
    constexpr size_t MAX_BUFFERED_MESSAGES = 32;
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
        constexpr uint32_t COE_SEGMENT_BAD_TOGGLE_BIT   = 0x104;

        constexpr uint32_t EOE_WRONG_SERVICE            = 0x201;

        // A Set IP slave result is surfaced directly in status_ (like CoE abort codes). EoE result
        // codes are small (0x0001/0x0002 would alias RUNNING/TIMEDOUT), so they are OR-ed with this
        // flag; the low 16 bits hold the ETG.1000.6 result code. result::SUCCESS maps to SUCCESS.
        constexpr uint32_t EOE_RESULT                   = 0x00010000;

        constexpr uint32_t FOE_WRONG_SERVICE            = 0x301;
        constexpr uint32_t FOE_CLIENT_BUFFER_TOO_SMALL  = 0x302;
        constexpr uint32_t FOE_PACKET_NUMBER            = 0x303;
        // A slave FoE error is surfaced like EOE_RESULT: low 16 bits hold the ETG.1000.6 error code.
        constexpr uint32_t FOE_RESULT                   = 0x00020000;
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
    class GatewayMessage final : public AbstractMessage
    {
    public:
        GatewayMessage(uint16_t mailbox_size, uint8_t const* raw_message, uint16_t gateway_index, nanoseconds timeout);

        /// \brief Build a GatewayMessage that is already completed: the reply is already in hand,
        ///        so there is no bus round-trip. Used by synchronous dispatch paths (e.g. the master OD,
        ///        ETG.1510). Starts in MessageStatus::SUCCESS so Gateway::processPendingRequests() picks
        ///        it up directly. Requires reply.size() >= sizeof(mailbox::Header).
        GatewayMessage(std::vector<uint8_t>&& reply, uint16_t gateway_index);

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
        void generateSMConfig(SyncManager::Register SM[2]);

        // messages factory
        std::shared_ptr<AbstractMessage> createSDO(uint16_t index, uint8_t subindex, bool CA, uint8_t request, void* data, uint32_t* data_size, nanoseconds timeout = 20ms);
        std::shared_ptr<GatewayMessage>  createGatewayMessage(uint8_t const* raw_message, int32_t raw_message_size, uint16_t gateway_index, nanoseconds timeout = 20ms);

        std::shared_ptr<AbstractMessage> createSDOInfoGetODList(CoE::SDO::information::ListType type, void* data,
                                                                uint32_t* data_size, nanoseconds timeout = 20ms);
        std::shared_ptr<AbstractMessage> createSDOInfoGetOD(uint16_t index, void* data, uint32_t* data_size,
                                                            nanoseconds timeout = 20ms);
        std::shared_ptr<AbstractMessage> createSDOInfoGetED(uint16_t index, uint8_t subindex, uint8_t value_info,
                                                            void* data, uint32_t* data_size, nanoseconds timeout = 20ms);

        // EoE (Ethernet over EtherCAT) - cf. ETG.1000.6 chapter 5.7
        std::shared_ptr<AbstractMessage> createEoESetIP(EoE::IpParameters const& params, nanoseconds timeout = 20ms);
        std::shared_ptr<AbstractMessage> createEoEGetIP(EoE::IpParameters* result, nanoseconds timeout = 20ms);
        // Fragment 'frame' into one unacknowledged message per EoE fragment. Returns the fragment count.
        size_t createEoESendFrame(uint8_t const* frame, size_t size, uint8_t port = 0);
        // Persistent listener reassembling inbound tunneled frames into eoe_frames.
        std::shared_ptr<AbstractMessage> createEoEFrameListener();

        // FoE (File over EtherCAT) - cf. ETG.1000.6 chapter 5.8
        std::shared_ptr<AbstractMessage> createFoERead(std::string const& file_name, uint32_t password,
                                                       void* data, uint32_t* data_size, nanoseconds timeout = 1s);
        std::shared_ptr<AbstractMessage> createFoEWrite(std::string const& file_name, uint32_t password,
                                                        void const* data, uint32_t data_size, nanoseconds timeout = 1s);

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

        std::vector<std::vector<uint8_t>> eoe_frames; // completed Ethernet frames received over EoE
        uint8_t eoe_frame_number{0};                  // 4-bit frame counter for outbound tunneling
    private:
    };
}

namespace kickcat::mailbox::response
{
    class Mailbox;
    class AbstractFileSystem;

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

        std::vector<uint8_t> data_;
        Mailbox* mailbox_;
    };

    /// \brief Response mailbox - it orchestrates the reception and the processing of messages (for slaves and gateway)
    class Mailbox
    {
        friend class AbstractMessage;
    public:
        Mailbox(AbstractESC* esc, uint16_t max_allocated_ram_by_msg, uint16_t max_msgs = 1);
        Mailbox(uint16_t max_allocated_ram_by_msg, uint16_t max_msgs = 1);

        ~Mailbox() = default;

        // Non-owning: references an application-owned dictionary that must outlive the mailbox.
        void enableCoE(CoE::Dictionary& dictionary);
        CoE::Dictionary& getDictionary(){return *dictionary_;}

        // EoE (Ethernet over EtherCAT). 'params' is application-owned and must outlive the mailbox;
        // Set IP writes into it, Get IP reads from it.
        void enableEoE(EoE::IpParameters& params);
        // Replaces any previous handler; invoked synchronously for each fully reassembled inbound frame.
        void setEoEFrameHandler(std::function<void(Mailbox&, uint8_t const*, size_t, uint8_t)> handler);
        EoE::IpParameters* eoeParameters() { return eoe_params_; }

        // FoE (File over EtherCAT). 'fs' is application-owned and must outlive the mailbox.
        void enableFoE(AbstractFileSystem& fs);
        AbstractFileSystem& fileSystem() { return *foe_fs_; }
        uint16_t maxMessageSize() const { return max_allocated_ram_by_msg_; }

        void deliverEoEFrame(uint8_t const* frame, size_t size, uint8_t port);
        // No-op if the mailbox cannot hold a fragment or the frame exceeds MAX_FRAGMENTED_FRAME.
        void sendEoEFrame(uint8_t const* frame, size_t size, uint8_t port = 0);

        std::vector<std::vector<uint8_t>> eoe_frames; // completed inbound frames (for tests / polling)

        // --- ESC-coupled methods (requires to pass the ESC to the constructor) ---
        int32_t configure();
        bool isConfigOk();
        void activate(bool is_activated);
        void receive();  // Try to receive a message from the ESC
        void send();     // Send a message in the to_send_ queue if any, keep it in the queue if the ESC is not ready yet

        // --- Core methods (ESC-independent) ---

        /// \brief Dispatch a raw mailbox message through existing handlers or factories
        /// \details Tries existing to_process_ handlers first, then creates new ones via factories.
        void handleMessage(std::vector<uint8_t>&& raw_message);

        /// \brief Process a message in the to_process_ queue if any
        void process();

        /// \brief Pop the next reply from the send queue
        /// \return The raw reply message, or an empty vector if the queue is empty
        std::vector<uint8_t> popReply();

        /// \brief Synchronous single-shot processing: dispatch, process, and return the reply
        std::vector<uint8_t> processRequest(std::vector<uint8_t>&& raw_message);

        /// \brief Serve an ETG.8200 gateway request synchronously against this OD and return a
        ///        completed (SUCCESS) GatewayMessage. Contrast with the slave-side
        ///        request::Mailbox::createGatewayMessage which enqueues a pending message.
        /// \return nullptr on malformed request, no/malformed reply, or oversized reply.
        std::shared_ptr<mailbox::request::GatewayMessage> serveGatewayRequest(
            uint8_t const* raw_message, int32_t raw_message_size, uint16_t gateway_index);

        // Access on the next message to send: mainly for unit test
        std::vector<uint8_t> const& readyToSend() const { return to_send_.front(); }

    private:
        void replyError(std::vector<uint8_t>&& raw_message, uint16_t code);

        AbstractESC* esc_;
        SyncManagerConfig mbx_in_{};
        SyncManagerConfig mbx_out_{};

        uint16_t max_allocated_ram_by_msg_;
        uint16_t max_msgs_;

        std::vector<std::function<std::shared_ptr<AbstractMessage>(Mailbox*, std::vector<uint8_t>&&)>> factories_;
        CoE::Dictionary* dictionary_{nullptr};         // application-owned, set by enableCoE
        EoE::IpParameters* eoe_params_{nullptr};       // application-owned, set by enableEoE
        AbstractFileSystem* foe_fs_{nullptr};          // application-owned, set by enableFoE
        std::function<void(Mailbox&, uint8_t const*, size_t, uint8_t)> eoe_frame_handler_;
        uint8_t eoe_frame_number_{0};                  // 4-bit frame counter for outbound tunneling

        std::list<std::shared_ptr<AbstractMessage>> to_process_;    /// Received messages, waiting to be processed
        std::queue<std::vector<uint8_t>> to_send_;                  /// Messages to send (replies from a received messages)

        std::vector<uint8_t> last_sent_{};                          /// store the last sent message in case of repeat requested
        std::vector<uint8_t> repeat_{};                             /// 'real' repeat, a copy of last sent WHEN the master fetch the mailbox
    };
}

#endif
