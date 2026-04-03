#ifndef KICKMSG_PUBLISHER_H
#define KICKMSG_PUBLISHER_H

#include <cstring>

#include "types.h"
#include "Region.h"
#include "kickcat/OS/Futex.h"

namespace kickmsg
{
    using kickcat::futex_wake_all;

    class Publisher
    {
    public:
        explicit Publisher(SharedRegion& region)
            : base_{region.base()}
            , header_{region.header()}
            , commit_timeout_{microseconds{header_->commit_timeout_us}}
            , pending_slot_{INVALID_SLOT}
            , pending_len_{0}
        {
        }

        Publisher(Publisher const&) = delete;
        Publisher& operator=(Publisher const&) = delete;
        Publisher(Publisher&&) noexcept = default;
        Publisher& operator=(Publisher&&) noexcept = default;

        void* allocate(std::size_t len)
        {
            if (len > header_->slot_data_size)
            {
                return nullptr;
            }

            auto slot_idx = treiber_pop(header_->free_top, base_, header_);
            if (slot_idx == INVALID_SLOT)
            {
                return nullptr;
            }

            pending_slot_ = slot_idx;
            pending_len_  = static_cast<uint32_t>(len);

            auto* slot = slot_at(base_, header_, slot_idx);
            return slot_data(slot);
        }

        std::size_t publish()
        {
            if (pending_slot_ == INVALID_SLOT)
            {
                return 0;
            }

            auto slot_idx = pending_slot_;
            auto len      = pending_len_;
            pending_slot_ = INVALID_SLOT;
            pending_len_  = 0;

            auto* slot     = slot_at(base_, header_, slot_idx);
            auto  capacity = header_->sub_ring_capacity;

            slot->refcount.store(static_cast<uint32_t>(header_->max_subs),
                                 std::memory_order_release);

            std::size_t delivered = 0;

            for (uint32_t i = 0; i < header_->max_subs; ++i)
            {
                auto* ring = sub_ring_at(base_, header_, i);
                if (ring->active.load(std::memory_order_acquire) == 0)
                {
                    continue;
                }

                uint64_t pos;
                do
                {
                    pos = ring->write_pos.load(std::memory_order_acquire);
                }
                while (not ring->write_pos.compare_exchange_weak(pos, pos + 1,
                           std::memory_order_acq_rel, std::memory_order_acquire));

                auto  idx     = pos & header_->sub_ring_mask;
                auto* entries = ring_entries(ring);
                auto& e       = entries[idx];

                if (pos >= capacity)
                {
                    uint64_t expected_seq = pos - capacity + 1;
                    if (wait_for_commit(e, expected_seq, commit_timeout_))
                    {
                        release_slot(e.slot_idx.load(std::memory_order_relaxed));
                    }
                }

                e.slot_idx.store(slot_idx, std::memory_order_relaxed);
                e.payload_len.store(len, std::memory_order_relaxed);
                e.sequence.store(pos + 1, std::memory_order_release);

                futex_wake_all(ring->write_pos);
                ++delivered;
            }

            auto excess = static_cast<uint32_t>(header_->max_subs)
                        - static_cast<uint32_t>(delivered);
            if (excess > 0)
            {
                auto prev = slot->refcount.fetch_sub(excess,
                                std::memory_order_acq_rel);
                if (prev == excess)
                {
                    treiber_push(header_->free_top, slot, slot_idx);
                }
            }

            return delivered;
        }

        bool send(void const* data, std::size_t len)
        {
            auto* ptr = allocate(len);
            if (ptr == nullptr)
            {
                return false;
            }
            std::memcpy(ptr, data, len);
            publish();
            return true;
        }

    private:
        static bool wait_for_commit(Entry& e, uint64_t expected_seq,
                                    microseconds timeout)
        {
            constexpr int CHECK_INTERVAL = 1024;
            auto deadline = steady_clock::now() + timeout;

            for (int i = 0; ; ++i)
            {
                if (e.sequence.load(std::memory_order_acquire) >= expected_seq)
                {
                    return true;
                }
                if ((i & (CHECK_INTERVAL - 1)) == 0 and i > 0)
                {
                    if (steady_clock::now() >= deadline)
                    {
                        return false;
                    }
                }
            }
        }

        void release_slot(uint32_t idx)
        {
            auto* s    = slot_at(base_, header_, idx);
            auto  prev = s->refcount.fetch_sub(1, std::memory_order_acq_rel);
            if (prev == 1)
            {
                treiber_push(header_->free_top, s, idx);
            }
        }

        void*                     base_;
        Header*                   header_;
        microseconds              commit_timeout_;
        uint32_t                  pending_slot_;
        uint32_t                  pending_len_;
    };

} // namespace kickmsg

#endif // KICKMSG_PUBLISHER_H
