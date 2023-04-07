#include <cstring>

#include "OS/Linux/VirtualSocket.h"
#include "Time.h"

namespace kickcat
{
    void VirtualSocket::createInterface(std::string const& interface)
    {
        SharedMemory shm;
        shm.open(interface, sizeof(VirtualQueue::Context) * 2);
        uint8_t* pos = reinterpret_cast<uint8_t*>(shm.address());

        VirtualQueue q1(pos);
        q1.initContext();

        VirtualQueue q2(pos + sizeof(VirtualQueue::Context));
        q2.initContext();
    }


    VirtualSocket::VirtualSocket(nanoseconds polling_period, bool side)
        : shm_{}
        , side_{side}
        , timeout_{2ms}
        , polling_period_{polling_period}
        , tx_{nullptr}
        , rx_{nullptr}
    {

    }


    void VirtualSocket::open(std::string const& interface)
    {
        shm_.open(interface, sizeof(VirtualQueue::Context) * 2);
        uint8_t* pos = reinterpret_cast<uint8_t*>(shm_.address());
        if (side_)
        {
            tx_ = VirtualQueue(pos);
            rx_ = VirtualQueue(pos + sizeof(VirtualQueue::Context));
        }
        else
        {
            rx_ = VirtualQueue(pos);
            tx_ = VirtualQueue(pos + sizeof(VirtualQueue::Context));
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

        nanoseconds deadline = since_epoch() + timeout_;
        do
        {
            auto packet = rx_.get(VirtualQueue::NON_BLOCKING);
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
