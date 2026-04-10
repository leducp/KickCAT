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

        uint32_t slot_idx = treiber_pop(header_->free_top, base_, header_);
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

        uint32_t slot_idx = pending_slot_;
        uint32_t len      = pending_len_;
        pending_slot_ = INVALID_SLOT;
        pending_len_  = 0;

        auto*    slot     = slot_at(base_, header_, slot_idx);
        uint64_t capacity = header_->sub_ring_capacity;

        // Pre-set refcount to max_subs before publishing to any ring,
        // so a fast eviction on ring[k] cannot free the slot before
        // we finish publishing to ring[k+1].
        slot->refcount.store(static_cast<uint32_t>(header_->max_subs),
                             std::memory_order_release);

        std::size_t delivered = 0;
        uint32_t    excess    = 0;

        for (uint32_t i = 0; i < header_->max_subs; ++i)
        {
            auto* ring = sub_ring_at(base_, header_, i);

            // Relaxed pre-check: skip obviously non-Live rings without
            // any RMW atomic. Stale reads are safe:
            //  - Sees Free, actually Live: miss one delivery (acceptable).
            //  - Sees Live, actually Draining: CAS catches it below.
            uint32_t snapshot = ring->state_flight.load(std::memory_order_relaxed);
            if (ring::get_state(snapshot) != ring::Live)
            {
                ++excess;
                continue;
            }

            // CAS admission: atomically verify state==Live and increment
            // in_flight. All ordering is on a single variable, so
            // acquire/release is sufficient (no seq_cst needed).
            uint32_t old = snapshot;
            bool admitted = false;
            while (true)
            {
                if (ring::get_state(old) != ring::Live)
                {
                    ++excess;
                    break;
                }
                if (ring->state_flight.compare_exchange_weak(old,
                        old + ring::IN_FLIGHT_ONE,
                        std::memory_order_acq_rel,
                        std::memory_order_acquire))
                {
                    admitted = true;
                    break;
                }
                // CAS failed — old was updated. Re-check state.
            }

            if (not admitted)
            {
                continue;
            }

            // Admitted: in_flight incremented, state is Live.

            // Claim a position in this ring. fetch_add is unconditional:
            // no CAS retry loop, O(1) under contention, and compiles to
            // a single LDADDAL on AArch64 with LSE atomics.
            uint64_t pos = ring->write_pos.fetch_add(1, std::memory_order_acq_rel);

            uint64_t idx  = pos & header_->sub_ring_mask;
            auto* entries = ring_entries(ring);
            auto& e       = entries[idx];

            // If the ring has wrapped, the entry we are about to overwrite
            // may still reference a live slot. Capture its slot_idx for
            // release AFTER we successfully lock the entry.
            //
            // We defer the release because of a TOCTOU race: between
            // wait_and_capture_slot reading the slot_idx and our lock CAS,
            // another publisher can overwrite the entry (evict + commit).
            // If our CAS then fails, releasing the captured slot_idx would
            // corrupt a live entry's refcount. Deferring until after lock
            // success guarantees we own the entry.
            uint32_t old_slot = INVALID_SLOT;
            if (pos >= capacity)
            {
                uint64_t expected_seq = pos - capacity + 1;
                old_slot = wait_and_capture_slot(e, expected_seq, commit_timeout_);
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
                ++excess;
                ring->state_flight.fetch_sub(ring::IN_FLIGHT_ONE,
                                             std::memory_order_release);
                continue;
            }

            // Lock succeeded: we exclusively own this entry. Release the
            // previous occupant's slot reference for this ring, unless
            // drain_unconsumed already released it (marked by INVALID_SLOT).
            if (old_slot != INVALID_SLOT)
            {
                uint32_t current_slot = e.slot_idx.load(std::memory_order_acquire);
                if (current_slot != INVALID_SLOT)
                {
                    release_slot(old_slot);
                }
            }

            // We exclusively own this entry. No other publisher can CAS from
            // LOCKED_SEQUENCE since they expect prev_seq.
            e.slot_idx.store(slot_idx, std::memory_order_relaxed);
            e.payload_len.store(len, std::memory_order_relaxed);

            // Release-store commits the entry: subscribers and future publishers
            // at this position will see all preceding stores.
            e.sequence.store(pos + 1, std::memory_order_release);

            // Release admission.
            ring->state_flight.fetch_sub(ring::IN_FLIGHT_ONE,
                                         std::memory_order_release);

            // Conditional wake: skip the syscall when no subscriber is blocking.
            if (ring->has_waiter.load(std::memory_order_relaxed))
            {
                futex_wake_all(ring->write_pos);
            }
            ++delivered;
        }

        // Batch release excess refs for all non-delivered rings.
        // Safe because: Free rings have no drain to race with, and
        // Draining rings where CAS failed never admitted us (in_flight
        // was never incremented), so their drain doesn't depend on us.
        if (excess > 0)
        {
            uint32_t prev = slot->refcount.fetch_sub(excess,
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
        nanoseconds start = kickcat::since_epoch();

        int i = 0;
        while (true)
        {
            uint64_t seq = e.sequence.load(std::memory_order_acquire);
            if (seq >= expected_seq and seq != LOCKED_SEQUENCE)
            {
                return e.slot_idx.load(std::memory_order_acquire);
            }
            ++i;
            if ((i & (CHECK_INTERVAL - 1)) == 0)
            {
                if (kickcat::elapsed_time(start) >= timeout)
                {
                    return INVALID_SLOT;
                }
            }
        }
    }

    void Publisher::release_slot(uint32_t idx)
    {
        auto*    s    = slot_at(base_, header_, idx);
        uint32_t prev = s->refcount.fetch_sub(1, std::memory_order_acq_rel);
        if (prev == 1)
        {
            treiber_push(header_->free_top, s, idx);
        }
    }
}
