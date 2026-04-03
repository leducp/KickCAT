#include <cstring>

#include "kickcat/Units.h"
#include "kickcat/TapSocket.h"
#include "kickcat/Error.h"

namespace kickcat
{
// LCOV_EXCL_START
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

        auto* base = static_cast<uint8_t*>(shm_.address());
        header_ = reinterpret_cast<Header*>(base);

        // Layout: [Header] [SlotPool] [Context A] [Context B] [Pool data]
        slot_pool_ = reinterpret_cast<SlotPool*>(base + align_up(sizeof(Header), CACHE_LINE));
        auto* ctx_a = reinterpret_cast<QUEUE::Context*>(reinterpret_cast<uint8_t*>(slot_pool_) + sizeof(SlotPool));
        auto* ctx_b = reinterpret_cast<QUEUE::Context*>(reinterpret_cast<uint8_t*>(ctx_a) + sizeof(QUEUE::Context));
        pool_base_  = reinterpret_cast<uint8_t*>(ctx_b) + sizeof(QUEUE::Context);

        if (init_)
        {
            std::memset(base, 0, 512_KiB);
            header_->magic   = MAGIC;
            header_->version = VERSION;
            header_->side_a_connected = 0;
            header_->side_b_connected = 0;
            slot_pool_->free_top = tagged_pack(0, INVALID_SLOT);

            // Push all slots into the free-stack
            for (uint32_t i = 0; i < POOL_SIZE; ++i)
            {
                treiber_push(slot_pool_->free_top, pool_base_, QUEUE::SLOT_STRIDE, i);
            }

            QUEUE queue_a{*ctx_a, *slot_pool_, pool_base_};
            queue_a.initContext();

            QUEUE queue_b{*ctx_b, *slot_pool_, pool_base_};
            queue_b.initContext();
        }
        else
        {
            if (header_->magic != MAGIC or header_->version != VERSION)
            {
                THROW_ERROR("open(): Incompatible shared memory layout (wrong magic or version)");
            }
        }

        // Role assignment via atomic CAS — no mutex needed
        uint8_t expected = 0;
        if (header_->side_a_connected.compare_exchange_strong(expected, 1, std::memory_order_acq_rel))
        {
            allocated_ = &header_->side_a_connected;
            in_  = std::make_unique<QUEUE>(*ctx_a, *slot_pool_, pool_base_);
            out_ = std::make_unique<QUEUE>(*ctx_b, *slot_pool_, pool_base_);
            return;
        }

        expected = 0;
        if (header_->side_b_connected.compare_exchange_strong(expected, 1, std::memory_order_acq_rel))
        {
            allocated_ = &header_->side_b_connected;
            in_  = std::make_unique<QUEUE>(*ctx_b, *slot_pool_, pool_base_);
            out_ = std::make_unique<QUEUE>(*ctx_a, *slot_pool_, pool_base_);
            return;
        }

        THROW_ERROR("open(): Socket is full");
    }

    void TapSocket::close() noexcept
    {
        if (allocated_)
        {
            allocated_->store(0, std::memory_order_release);
            allocated_ = nullptr;
        }
    }

    void TapSocket::setTimeout(nanoseconds timeout)
    {
        timeout_ = timeout;
    }

    int32_t TapSocket::read(void* frame, int32_t frame_size)
    {
        auto item = in_->get(timeout_);
        if (item.address == nullptr)
        {
            return -EAGAIN;
        }

        int32_t toCopy = std::min(static_cast<int32_t>(item.len), frame_size);
        std::memcpy(frame, item.address, toCopy);

        in_->free(item);

        return toCopy;
    }

    int32_t TapSocket::write(void const* frame, int32_t frame_size)
    {
        auto item = out_->allocate();
        if (item.address == nullptr)
        {
            return -EAGAIN;
        }

        int32_t toCopy = std::min(static_cast<int32_t>(QUEUE::item_size()), frame_size);
        std::memcpy(item.address, frame, toCopy);
        item.len = static_cast<uint32_t>(toCopy); //TODO: compute FCS!

        out_->ready(item);

        return toCopy;
    }
// LCOV_EXCL_STOP
}
