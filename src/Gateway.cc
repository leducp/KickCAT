#include <cstring>
#include "Gateway.h"

namespace kickcat
{
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
            DEBUG_PRINT("Frame size is too small\n");
            return;
        }

        // -> Frame command type
        if (header->type != EthercatType::MAILBOX)
        {
            DEBUG_PRINT("Frame command is not MAILBOX but %d\n", header->type);
            return;
        }

        // -> Message size coherency (frame size shall be able to contain message size)
        int32_t raw_message_size = static_cast<int32_t>(sizeof(mailbox::Header) + mbx_header->len);
        int32_t max_message_size = static_cast<int32_t>(frame_size - sizeof(EthercatHeader));
        if (raw_message_size > max_message_size)
        {
            DEBUG_PRINT("Message size is bigger than frame size\n");
            return;
        }

        uint8_t const* raw_message = frame + sizeof(EthercatHeader);
        auto msg = addMessage_(raw_message, raw_message_size , index);
        if (msg == nullptr)
        {
            return;
        }

        DEBUG_PRINT("New request (%d - %d)\n", frame_size, index);
        pendingRequests_.push_back(msg);
    }


    void Gateway::processPendingRequests()
    {
        uint8_t frame[ETH_MTU_SIZE];
        EthercatHeader* const header = reinterpret_cast<EthercatHeader*>(frame);

        pendingRequests_.erase(std::remove_if(pendingRequests_.begin(), pendingRequests_.end(),
            [&](std::shared_ptr<GatewayMessage> msg)-> bool
            {
                if (msg->status() != MessageStatus::SUCCESS)
                {
                    return false;
                }

                header->type = EthercatType::MAILBOX;
                header->len  = msg->size() & 0x7ff;
                std::memcpy(frame + sizeof(EthercatHeader), msg->data(), msg->size());
                socket_->sendTo(frame, static_cast<int32_t>(msg->size() + sizeof(EthercatHeader)), msg->gatewayIndex());

                return true;
            }),
            pendingRequests_.end());
    }
}
