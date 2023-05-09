#include <cstring>

#include "OS/Linux/VirtualSocket.h"
#include "Time.h"

namespace kickcat
{
    void VirtualSocket::createInterface(std::string const& interface)
    {
        SharedMemory shm;
        shm.open(interface, sizeof(Context));
        Context* ctx = reinterpret_cast<Context*>(shm.address());

        Mutex mutex(ctx->lock);
        mutex.init();

        {
            VirtualQueue q(ctx->q1);
            q.initContext();
        }

        {
            VirtualQueue q(ctx->q2);
            q.initContext();
        }
    }


    VirtualSocket::VirtualSocket(nanoseconds polling_period)
        : shm_{}
        , timeout_{2ms}
        , polling_period_{polling_period}
        , tx_{nullptr}
        , rx_{nullptr}
    {

    }


    void VirtualSocket::open(std::string const& interface)
    {
        shm_.open(interface, sizeof(Context));
        Context* ctx = reinterpret_cast<Context*>(shm_.address());
        Mutex mutex(ctx->lock);

        LockGuard lock(mutex);
        if (ctx->side)
        {
            tx_ = VirtualQueue(ctx->q1);
            rx_ = VirtualQueue(ctx->q2);
            ctx->side = false;
        }
        else
        {
            tx_ = VirtualQueue(ctx->q2);
            rx_ = VirtualQueue(ctx->q1);
            ctx->side = true;
        }
    }


    void VirtualSocket::setTimeout(nanoseconds timeout)
    {
        timeout_ = timeout;
    }


    void VirtualSocket::close() noexcept
    {

    }


    int32_t VirtualSocket::read(uint8_t* frame, int32_t frame_size)
    {
        VirtualQueue::Mode mode = VirtualQueue::NON_BLOCKING;
        if (timeout_ < 0ns)
        {
            mode = VirtualQueue::BLOCKING;
        }

        nanoseconds deadline = since_epoch() + timeout_;
        do
        {
            auto packet = rx_.get(mode);
            if (packet.index == SBUF_INVALID_INDEX)
            {
                sleep(polling_period_);
                continue;
            }

            int32_t len = static_cast<int32_t>(packet.len);
            int32_t to_copy = std::min(frame_size, len);
            std::memcpy(frame, packet.address, to_copy);
            rx_.free(packet);
            return to_copy;
        } while (since_epoch() < deadline);

        errno = ETIMEDOUT;
        return -1;
    }


    int32_t VirtualSocket::write(uint8_t const* frame, int32_t frame_size)
    {
        auto packet = tx_.alloc(VirtualQueue::NON_BLOCKING);
        if (packet.index == SBUF_INVALID_INDEX)
        {
            errno = EAGAIN;
            return -1;
        }

        // Write data and finalize operation
        std::memcpy(packet.address, frame, frame_size);
        packet.len = frame_size;
        tx_.ready(packet);
        return frame_size;
    }
}
