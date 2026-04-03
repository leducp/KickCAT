#ifndef KICKCAT_TAP_SOCKET_H
#define KICKCAT_TAP_SOCKET_H

#include <memory>

#include "kickcat/AbstractSocket.h"
#include "kickcat/SpscQueue.h"
#include "kickcat/OS/SharedMemory.h"

namespace kickcat
{
    class TapSocket final : public AbstractSocket
    {
    public:
        using QUEUE = SpscQueue<uint8_t[1522], 64>;

        static constexpr uint32_t MAGIC   = 0x4B435441; // "KCTA"
        static constexpr uint32_t VERSION = 2;
        static constexpr uint32_t POOL_SIZE = 2 * QUEUE::depth(); // shared pool for both directions

        struct Header
        {
            uint32_t magic;
            uint32_t version;
            std::atomic<uint8_t> side_a_connected;
            std::atomic<uint8_t> side_b_connected;
        };

        TapSocket(bool init = false);
        virtual ~TapSocket();

        void open(std::string const& interface) override;
        void close() noexcept override;
        /// Set the timeout to receive a frame. If negative, the timeout is infinite (blocking call).
        void setTimeout(nanoseconds timeout) override;

        int32_t read(void* frame, int32_t frame_size) override;
        int32_t write(void const* frame, int32_t frame_size) override;

    private:
        bool init_;
        nanoseconds timeout_;
        SharedMemory shm_{};

        Header* header_{nullptr};
        SlotPool* slot_pool_{nullptr};
        std::atomic<uint8_t>* allocated_{nullptr};
        void* pool_base_{nullptr};

        std::unique_ptr<QUEUE> in_{nullptr};
        std::unique_ptr<QUEUE> out_{nullptr};
    };
}

#endif
