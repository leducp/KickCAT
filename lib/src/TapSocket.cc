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

    // Shared memory layout per direction:
    //   [SlotPool] [Context] [Pool data: POOL_PER_DIRECTION * SLOT_STRIDE bytes]
    struct DirectionLayout
    {
        SlotPool*             slot_pool;
        TapSocket::QUEUE::Context* ctx;
        void*                 pool_base;
    };

    static DirectionLayout layout_direction(uint8_t* base)
    {
        auto* sp  = reinterpret_cast<SlotPool*>(base);
        auto* ctx = reinterpret_cast<TapSocket::QUEUE::Context*>(
            base + sizeof(SlotPool));
        auto* pool = base + sizeof(SlotPool) + sizeof(TapSocket::QUEUE::Context);
        return {sp, ctx, pool};
    }

    static constexpr std::size_t direction_size()
    {
        return sizeof(SlotPool)
             + sizeof(TapSocket::QUEUE::Context)
             + TapSocket::POOL_PER_DIRECTION * TapSocket::QUEUE::SLOT_STRIDE;
    }

    void TapSocket::open(std::string const& interface)
    {
        shm_.open(interface, 512_KiB);

        auto* base = static_cast<uint8_t*>(shm_.address());
        header_ = reinterpret_cast<Header*>(base);

        // Layout: [Header] [Direction A] [Direction B]
        auto* dir_a_base = base + align_up(sizeof(Header), CACHE_LINE);
        auto* dir_b_base = dir_a_base + align_up(direction_size(), CACHE_LINE);
        auto dir_a = layout_direction(dir_a_base);
        auto dir_b = layout_direction(dir_b_base);

        if (init_)
        {
            std::memset(base, 0, 512_KiB);
            header_->magic   = MAGIC;
            header_->version = VERSION;
            header_->side_a_connected = 0;
            header_->side_b_connected = 0;

            // Initialize each direction's pool and ring
            auto init_direction = [](DirectionLayout const& dir)
            {
                dir.slot_pool->free_top = tagged_pack(0, INVALID_SLOT);
                for (uint32_t i = 0; i < POOL_PER_DIRECTION; ++i)
                {
                    treiber_push(dir.slot_pool->free_top, dir.pool_base, QUEUE::SLOT_STRIDE, i);
                }
                QUEUE queue{*dir.ctx, *dir.slot_pool, dir.pool_base};
                queue.initContext();
            };
            init_direction(dir_a);
            init_direction(dir_b);
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
            in_  = std::make_unique<QUEUE>(*dir_a.ctx, *dir_a.slot_pool, dir_a.pool_base);
            out_ = std::make_unique<QUEUE>(*dir_b.ctx, *dir_b.slot_pool, dir_b.pool_base);
            return;
        }

        expected = 0;
        if (header_->side_b_connected.compare_exchange_strong(expected, 1, std::memory_order_acq_rel))
        {
            allocated_ = &header_->side_b_connected;
            in_  = std::make_unique<QUEUE>(*dir_b.ctx, *dir_b.slot_pool, dir_b.pool_base);
            out_ = std::make_unique<QUEUE>(*dir_a.ctx, *dir_a.slot_pool, dir_a.pool_base);
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
