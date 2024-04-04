#ifndef KICKCAT_TAP_SOCKET_H
#define KICKCAT_TAP_SOCKET_H

#include <memory>

#include "kickcat/AbstractSocket.h"
#include "kickcat/SBufQueue.h"
#include "kickcat/OS/SharedMemory.h"
#include "kickcat/OS/Mutex.h"

namespace kickcat
{
    class TapSocket final : public AbstractSocket
    {
    public:
        TapSocket(bool init=false);
        virtual ~TapSocket();

        void open(std::string const& interface) override;
        void close() noexcept override;

        /// Set the timeout to receive a frame. If negative, the timeout is infinite (blocking call).
        void setTimeout(nanoseconds timeout) override;

        int32_t read(uint8_t* frame, int32_t frame_size) override;
        int32_t write(uint8_t const* frame, int32_t frame_size) override;

    private:
        bool init_;
        nanoseconds timeout_;
        SharedMemory shm_{};

        using QUEUE = SBufQueue<uint8_t[1522], 64>;
        std::unique_ptr<QUEUE> in_ {nullptr};
        std::unique_ptr<QUEUE> out_{nullptr};

        struct Metadata
        {
            os_mutex mutex;
            uint8_t a_to_b;
            uint8_t b_to_a;
        };
        uint8_t* allocated_{nullptr};
    };
}

#endif
