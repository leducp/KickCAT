#include <system_error>
#include <cstring>

#include "kickcat/Units.h"
#include "kickcat/TapSocket.h"

namespace kickcat
{
    TapSocket::TapSocket(bool init)
        : init_{init}
    {
        setTimeout(0ns);
    }

    TapSocket::~TapSocket()
    {
        close();
    }

    void TapSocket::open(std::string const& interface)
    {
        shm_.open(interface, 512_KiB);

        struct Metadata* metadata = reinterpret_cast<struct Metadata*>(shm_.address());
        Mutex mutex(&metadata->mutex);
        QUEUE::Context* queue_a_address = reinterpret_cast<QUEUE::Context*>(reinterpret_cast<uint8_t*>(shm_.address()) + sizeof(Metadata));
        QUEUE::Context* queue_b_address = queue_a_address + 1;
        if (init_)
        {
            std::memset(shm_.address(), 0, 512_KiB);
            mutex.init();

            QUEUE queue_a{queue_a_address};
            queue_a.initContext();

            QUEUE queue_b{queue_b_address};
            queue_b.initContext();
        }

        LockGuard lock(mutex);
        if (metadata->a_to_b == 0)
        {
            allocated_ = &metadata->a_to_b;
            *allocated_ = 1;
            in_  = std::make_unique<QUEUE>(queue_a_address);
            out_ = std::make_unique<QUEUE>(queue_b_address);

            return;
        }

        if (metadata->b_to_a == 0)
        {
            allocated_ = &metadata->b_to_a;
            *allocated_ = 1;
            in_  = std::make_unique<QUEUE>(queue_b_address);
            out_ = std::make_unique<QUEUE>(queue_a_address);

            return;
        }

        THROW_ERROR("open(): Socket is full");
    }

    void TapSocket::close() noexcept
    {
        if (allocated_)
        {
            *allocated_ = 0;
        }
    }

    void TapSocket::setTimeout(nanoseconds timeout)
    {
        timeout_ = timeout;
    }

    int32_t TapSocket::read(uint8_t* frame, int32_t frame_size)
    {
        auto item = in_->get(timeout_);
        if (item.address == nullptr)
        {
            return -1;
        }

        int32_t toCopy = std::min(static_cast<int32_t>(item.len), frame_size);
        std::memcpy(frame, item.address, toCopy);

        in_->free(item);

        return toCopy;
    }

    int32_t TapSocket::write(uint8_t const* frame, int32_t frame_size)
    {
        auto item = out_->allocate(timeout_);
        if (item.address == nullptr)
        {
            return -1;
        }

        int32_t toCopy = std::min(static_cast<int32_t>(QUEUE::item_size()), frame_size);
        std::memcpy(item.address, frame, frame_size);
        item.len = static_cast<uint32_t>(toCopy); //TODO: compute FCS!

        out_->ready(item);

        return toCopy;
    }
}
