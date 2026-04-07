#include "kickmsg/Publisher.h"

namespace kickmsg
{
    Publisher::~Publisher()
    {
        release_pending();
    }

    void Publisher::release_pending()
    {
        if (pending_slot_ != INVALID_SLOT)
        {
            // Return the uncommitted slot to the free-stack.
            auto* slot = slot_at(base_, header_, pending_slot_);
            treiber_push(header_->free_top, slot, pending_slot_);
            pending_slot_ = INVALID_SLOT;
            pending_len_  = 0;
        }
    }

    void* Publisher::allocate(std::size_t len)
    {
        if (len > header_->slot_data_size)
        {
            return nullptr;
        }

        // Release any previously allocated but unpublished slot.
        release_pending();

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

    std::size_t Publisher::publish()
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

        // Pre-set refcount to max_subs before publishing to any ring,
        // so a fast eviction on ring[k] cannot free the slot before
        // we finish publishing to ring[k+1].
        slot->refcount.store(static_cast<uint32_t>(header_->max_subs),
                             std::memory_order_release);

        std::size_t delivered = 0;

        for (uint32_t i = 0; i < header_->max_subs; ++i)
        {
            auto* ring = sub_ring_at(base_, header_, i);

            // Announce presence before checking active, so the subscriber's
            // destructor can wait for us to finish via in_flight == 0.
            ring->in_flight.fetch_add(1, std::memory_order_release);

            if (ring->active.load(std::memory_order_acquire) == 0)
            {
                ring->in_flight.fetch_sub(1, std::memory_order_release);
                continue;
            }

            // Claim a position in this ring via CAS (MPSC serialization).
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

            // If the ring has wrapped, the entry we are about to overwrite
            // may still reference a live slot. Wait for the previous writer
            // to commit, then release that slot's reference for this ring.
            if (pos >= capacity)
            {
                uint64_t expected_seq = pos - capacity + 1;
                uint32_t old_slot = wait_and_capture_slot(e, expected_seq, commit_timeout_);
                if (old_slot != INVALID_SLOT)
                {
                    release_slot(old_slot);
                }
            }

            // Two-phase commit: CAS sequence to LOCKED_SEQUENCE to exclusively
            // own the entry, write data, then release-store the final sequence.
            // This prevents concurrent publishers from interleaving slot_idx writes.
            uint64_t prev_seq = 0;
            if (pos >= capacity)
            {
                prev_seq = pos - capacity + 1;
            }
            bool locked = false;
            for (int attempt = 0; attempt < 64; ++attempt)
            {
                uint64_t expected = prev_seq;
                // Acquire on success: we need to see the previous writer's stores.
                if (e.sequence.compare_exchange_weak(expected, LOCKED_SEQUENCE,
                        std::memory_order_acquire, std::memory_order_relaxed))
                {
                    locked = true;
                    break;
                }
                if (expected != LOCKED_SEQUENCE)
                {
                    // Entry was committed by another publisher, not just locked.
                    break;
                }
                // Another publisher holds the lock; it will release quickly.
            }
            if (not locked)
            {
                ++dropped_;
                ring->in_flight.fetch_sub(1, std::memory_order_release);
                continue;
            }

            // We exclusively own this entry. No other publisher can CAS from
            // LOCKED_SEQUENCE since they expect prev_seq.
            e.slot_idx.store(slot_idx, std::memory_order_relaxed);
            e.payload_len.store(len, std::memory_order_relaxed);

            // Release-store commits the entry: subscribers and future publishers
            // at this position will see all preceding stores.
            e.sequence.store(pos + 1, std::memory_order_release);

            ring->in_flight.fetch_sub(1, std::memory_order_release);
            futex_wake_all(ring->write_pos);
            ++delivered;
        }

        // Subtract references for rings we skipped (inactive or lock failed).
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

    int32_t Publisher::send(void const* data, std::size_t len)
    {
        if (len > header_->slot_data_size)
        {
            return -EMSGSIZE;
        }

        auto* ptr = allocate(len);
        if (ptr == nullptr)
        {
            return -EAGAIN;
        }

        std::memcpy(ptr, data, len);
        publish();
        return static_cast<int32_t>(len);
    }

    uint32_t Publisher::wait_and_capture_slot(Entry& e, uint64_t expected_seq,
                                              microseconds timeout)
    {
        constexpr int CHECK_INTERVAL = 1024;
        auto deadline = steady_clock::now() + timeout;

        int i = 0;
        while (true)
        {
            auto seq = e.sequence.load(std::memory_order_acquire);
            if (seq >= expected_seq and seq != LOCKED_SEQUENCE)
            {
                // Acquire: pairs with drain_unconsumed's release store of INVALID_SLOT
                // after releasing the ring's reference. Without acquire, a publisher
                // on AArch64/RISC-V could read a stale slot_idx and double-decrement.
                return e.slot_idx.load(std::memory_order_acquire);
            }
            ++i;
            if ((i & (CHECK_INTERVAL - 1)) == 0)
            {
                if (steady_clock::now() >= deadline)
                {
                    return INVALID_SLOT;
                }
            }
        }
    }

    void Publisher::release_slot(uint32_t idx)
    {
        auto* s    = slot_at(base_, header_, idx);
        auto  prev = s->refcount.fetch_sub(1, std::memory_order_acq_rel);
        if (prev == 1)
        {
            treiber_push(header_->free_top, s, idx);
        }
    }
} // namespace kickmsg
