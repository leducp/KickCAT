#ifndef KICKCAT_SPSC_QUEUE_H
#define KICKCAT_SPSC_QUEUE_H

#include <atomic>
#include <cstdint>
#include <cstring>

#include "kickcat/Treiber.h"
#include "kickcat/OS/Futex.h"

namespace kickcat
{
    struct SpscEntry
    {
        std::atomic<uint64_t> sequence;    // commit barrier: set to (pos + 1) once entry is ready
        std::atomic<uint32_t> slot_idx;
        std::atomic<uint32_t> payload_len;
    };

    static_assert(sizeof(SpscEntry) == 16, "SpscEntry must be 16 bytes");

    /// Lock-free SPSC queue backed by a shared Treiber free-stack.
    /// Single producer writes frames, single consumer reads them.
    /// Designed to live entirely in shared memory for cross-process IPC.
    ///
    /// Template parameters:
    ///   T — frame buffer type (e.g. uint8_t[1522])
    ///   N — ring capacity, must be a power of two
    template<typename T, uint32_t N>
    class SpscQueue
    {
        static_assert(N > 0 and (N & (N - 1)) == 0, "N must be a power of two");
        static constexpr uint32_t MASK = N - 1;

    public:
        static constexpr std::size_t SLOT_STRIDE = align_up(sizeof(SlotMeta) + sizeof(T), CACHE_LINE);

        /// Shared memory layout for one direction.
        struct Context
        {
            alignas(CACHE_LINE) std::atomic<uint64_t> write_pos;
            alignas(CACHE_LINE) uint64_t read_pos;
            SpscEntry entries[N];
        };

        struct Item
        {
            uint32_t index;
            uint32_t len;
            T* address;
        };

        SpscQueue(Context& ctx, std::atomic<uint64_t>& free_top, void* pool_base)
            : ctx_{&ctx}, free_top_{&free_top}, pool_base_{pool_base}
        {}

        /// Initialize the context (zeroes ring, resets positions).
        /// Must be called exactly once by the process that creates the shared memory.
        void initContext()
        {
            ctx_->write_pos.store(0, std::memory_order_relaxed);
            ctx_->read_pos = 0;
            for (uint32_t i = 0; i < N; ++i)
            {
                ctx_->entries[i].sequence.store(0, std::memory_order_relaxed);
                ctx_->entries[i].slot_idx.store(INVALID_SLOT, std::memory_order_relaxed);
                ctx_->entries[i].payload_len.store(0, std::memory_order_relaxed);
            }
        }

        /// Producer: allocate a free slot to write into.
        /// Returns an item with address == nullptr if no slot is available.
        Item allocate()
        {
            uint32_t idx = treiber_pop(*free_top_, pool_base_, SLOT_STRIDE);
            if (idx == INVALID_SLOT)
            {
                return {INVALID_SLOT, 0, nullptr};
            }
            auto* meta = slot_meta_at(pool_base_, SLOT_STRIDE, idx);
            return {idx, 0, reinterpret_cast<T*>(slot_data(meta))};
        }

        /// Producer: publish a filled item into the ring.
        /// The sequence store (release) acts as the commit barrier — the consumer
        /// will not see this entry until sequence is written.
        void ready(Item const& item)
        {
            uint64_t pos = ctx_->write_pos.load(std::memory_order_relaxed);
            auto& e = ctx_->entries[pos & MASK];

            // If ring is full, evict the oldest entry and return its slot to the free-stack
            if (pos >= N)
            {
                uint32_t old_slot = e.slot_idx.load(std::memory_order_relaxed);
                if (old_slot != INVALID_SLOT)
                {
                    treiber_push(*free_top_, pool_base_, SLOT_STRIDE, old_slot);
                }
            }

            e.slot_idx.store(item.index, std::memory_order_relaxed);
            e.payload_len.store(item.len, std::memory_order_relaxed);
            // Commit barrier: consumer sees entry only after this release store.
            // Pairs with the acquire load in get().
            e.sequence.store(pos + 1, std::memory_order_release);

            // Advance write_pos (single producer, no CAS needed) and wake consumer.
            // Release: ensures the entry writes above are visible before write_pos advances.
            ctx_->write_pos.store(pos + 1, std::memory_order_release);
            futex_wake_all(ctx_->write_pos);
        }

        /// Consumer: get the next available frame.
        /// Blocks up to \p timeout. Returns an item with address == nullptr on timeout.
        Item get(nanoseconds timeout)
        {
            uint64_t rp = ctx_->read_pos;

            // Fast path: check if data is already available
            // Acquire: pairs with write_pos release store in ready()
            uint64_t wp = ctx_->write_pos.load(std::memory_order_acquire);
            if (wp <= rp)
            {
                if (timeout == 0ns)
                {
                    return {INVALID_SLOT, 0, nullptr};
                }
                if (not wait_for_data(rp, timeout))
                {
                    return {INVALID_SLOT, 0, nullptr};
                }
            }

            auto& e = ctx_->entries[rp & MASK];

            // Acquire: pairs with sequence release store in ready() — ensures we see slot_idx and payload_len
            uint64_t seq = e.sequence.load(std::memory_order_acquire);
            if (seq != rp + 1)
            {
                return {INVALID_SLOT, 0, nullptr};
            }

            uint32_t slot_idx    = e.slot_idx.load(std::memory_order_relaxed);
            uint32_t payload_len = e.payload_len.load(std::memory_order_relaxed);

            ctx_->read_pos = rp + 1;

            auto* meta = slot_meta_at(pool_base_, SLOT_STRIDE, slot_idx);
            return {slot_idx, payload_len, reinterpret_cast<T*>(slot_data(meta))};
        }

        /// Consumer: return a consumed slot to the free-stack.
        void free(Item const& item)
        {
            treiber_push(*free_top_, pool_base_, SLOT_STRIDE, item.index);
        }

        constexpr static std::size_t item_size() { return sizeof(T); }
        constexpr static uint32_t    depth()     { return N; }

    private:
        /// Block until write_pos > read_pos or timeout.
        bool wait_for_data(uint64_t read_pos, nanoseconds timeout)
        {
            if (timeout < 0ns)
            {
                // Infinite wait
                for (;;)
                {
                    uint64_t wp = ctx_->write_pos.load(std::memory_order_acquire);
                    if (wp > read_pos)
                    {
                        return true;
                    }
                    futex_wait(ctx_->write_pos, wp, 1s);
                }
            }

            auto deadline = since_epoch() + timeout;
            for (;;)
            {
                uint64_t wp = ctx_->write_pos.load(std::memory_order_acquire);
                if (wp > read_pos)
                {
                    return true;
                }
                nanoseconds remaining = deadline - since_epoch();
                if (remaining <= 0ns)
                {
                    return false;
                }
                futex_wait(ctx_->write_pos, wp, remaining);
            }
        }

        Context*               ctx_;
        std::atomic<uint64_t>* free_top_;
        void*                  pool_base_;
    };
}

#endif
