#ifndef KICKCAT_FOE_MAILBOX_REQUEST_H
#define KICKCAT_FOE_MAILBOX_REQUEST_H

#include <string>

#include "kickcat/Mailbox.h"
#include "kickcat/FoE/protocol.h"

namespace kickcat::mailbox::request
{
    // Read a file from a slave (ETG.1000.6 FoE Read Request). Drives the Data/Ack loop and fills the
    // caller buffer. On a slave error, status() is (MessageStatus::FOE_RESULT | <FoE error code>).
    class FoEReadMessage final : public AbstractMessage
    {
    public:
        FoEReadMessage(uint16_t mailbox_size, uint16_t reply_size, std::string const& file_name,
                       uint32_t password, void* data, uint32_t* data_size, nanoseconds timeout);
        virtual ~FoEReadMessage() = default;

        ProcessingResult process(uint8_t const* received) override;

    private:
        ProcessingResult sendAck(uint32_t packet_number, bool last);

        uint8_t*  client_data_;
        uint32_t* client_data_size_;
        uint32_t  client_buffer_size_;
        uint32_t  received_size_{0};
        uint32_t  expected_packet_{1};
        uint32_t  max_data_;            // slave's max Data payload; a shorter packet ends the transfer
    };

    // Write a file to a slave (ETG.1000.6 FoE Write Request). Streams 'data' as segmented Data Requests.
    class FoEWriteMessage final : public AbstractMessage
    {
    public:
        FoEWriteMessage(uint16_t mailbox_size, std::string const& file_name, uint32_t password,
                        void const* data, uint32_t data_size, nanoseconds timeout);
        virtual ~FoEWriteMessage() = default;

        ProcessingResult process(uint8_t const* received) override;

    private:
        void prepareData();

        uint8_t const* client_data_;
        uint32_t  client_data_size_;
        uint32_t  sent_size_{0};
        uint32_t  packet_number_{0};
        uint32_t  max_data_;            // our max Data payload per packet
        bool      last_sent_{false};
    };
}

#endif
