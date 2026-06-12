#include <cstring>
#include <inttypes.h>
#include <algorithm>

#include "Gateway.h"
#include "debug.h"

namespace kickcat
{
    using namespace mailbox::request;

    void Gateway::fetchRequest()
    {
        uint8_t frame[ETH_MTU_SIZE];
        EthercatHeader  const* const header     = reinterpret_cast<EthercatHeader  const*>(frame);
        mailbox::Header const* const mbx_header = reinterpret_cast<mailbox::Header const*>(frame + sizeof(EthercatHeader));

        auto [frame_size, index] = socket_->recv(frame, ETH_MTU_SIZE);
        if (frame_size < 0)
        {
            return;
        }

        // Check frame validity
        // -> minimal size
        constexpr int32_t MIN_EXPECTED = sizeof(EthercatHeader) + sizeof(mailbox::Header);
        if (frame_size < MIN_EXPECTED)
        {
            // Payload shall contain a valid mailbox EtherCAT frame
            gateway_error("Frame size is too small\n");
            return;
        }

        // -> Frame command type
        if (header->type != EthercatType::MAILBOX)
        {
            gateway_error("Frame command is not MAILBOX but %d\n", header->type);
            return;
        }

        // -> Message size coherency (frame size shall be able to contain message size)
        int32_t raw_message_size = static_cast<int32_t>(sizeof(mailbox::Header) + mbx_header->len);
        int32_t max_message_size = static_cast<int32_t>(frame_size - sizeof(EthercatHeader));
        if (raw_message_size > max_message_size)
        {
            gateway_error("Message size is bigger than frame size\n");
            return;
        }

        uint8_t const* raw_message = frame + sizeof(EthercatHeader);
        auto msg = addMessage_(raw_message, raw_message_size , index);
        if (msg == nullptr)
        {
            return;
        }

        gateway_info("New request (%" PRIi32 " - %d)\n", frame_size, index);
        pendingRequests_.push_back(msg);
    }


    void Gateway::processPendingRequests()
    {
        uint8_t frame[ETH_MTU_SIZE];
        EthercatHeader* const header = reinterpret_cast<EthercatHeader*>(frame);

        pendingRequests_.erase(std::remove_if(pendingRequests_.begin(), pendingRequests_.end(),
            [&](std::shared_ptr<GatewayMessage> msg)-> bool
            {
                uint32_t const status = msg->status();
                if (status == MessageStatus::RUNNING)
                {
                    return false;
                }

                if (status != MessageStatus::SUCCESS)
                {
                    // Failed requests would otherwise accumulate forever: drop them without a reply.
                    gateway_error("Request %d failed with status 0x%" PRIx32 "\n", msg->gatewayIndex(), status);
                    return true;
                }

                size_t const reply_size = std::min(msg->size(), sizeof(frame) - sizeof(EthercatHeader));
                header->type = EthercatType::MAILBOX;
                header->len  = reply_size & 0x7ff;
                std::memcpy(frame + sizeof(EthercatHeader), msg->data(), reply_size);
                socket_->sendTo(frame, static_cast<int32_t>(reply_size + sizeof(EthercatHeader)), msg->gatewayIndex());

                return true;
            }),
            pendingRequests_.end());
    }
}
