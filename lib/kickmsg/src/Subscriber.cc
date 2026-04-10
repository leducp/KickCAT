#include "kickmsg/Subscriber.h"

#include "kickcat/OS/Time.h"

namespace kickmsg
{
    Subscriber::Subscriber(SharedRegion& region)
        : base_{region.base()}
        , header_{region.header()}
        , ring_idx_{UINT32_MAX}
        , start_pos_{0}
        , read_pos_{0}
        , lost_{0}
    {
        recv_buf_.resize(header_->slot_data_size);

        for (uint32_t i = 0; i < header_->max_subs; ++i)
        {
            auto* ring = sub_ring_at(base_, header_, i);
            // Requires Free | in_flight=0. A ring stuck at Free | in_flight>0
            // (from a crashed publisher) stays retired until the operator
            // calls reset_retired_rings(). We do NOT force-reset stale
            // in_flight: the packed layout means a late fetch_sub from a
            // slow publisher would underflow into the state bits.
            uint32_t expected = ring::make_packed(ring::Free);
            // Capture write_pos BEFORE setting Live. Once Live, publishers
            // can immediately commit via fetch_add, racing with our read.
            // Reading first ensures start_pos_ <= any position a publisher
            // can claim after seeing Live.
            uint64_t wp = ring->write_pos.load(std::memory_order_acquire);
            if (ring->state_flight.compare_exchange_strong(expected,
                    ring::make_packed(ring::Live),
                    std::memory_order_acq_rel))
            {
                ring_idx_  = i;
                start_pos_ = wp;
                read_pos_  = start_pos_;
                break;
            }
        }

        if (ring_idx_ == UINT32_MAX)
        {
            throw std::runtime_error("No free subscriber slots");
        }
    }

    void Subscriber::release_ring()
    {
        if (ring_idx_ == UINT32_MAX)
        {
            return;
        }

        auto* ring = sub_ring_at(base_, header_, ring_idx_);

        // Transition Live → Draining, preserving in_flight count.
        uint32_t old = ring->state_flight.load(std::memory_order_acquire);
        while (true)
        {
            uint32_t desired = (old & ~ring::STATE_MASK) | ring::Draining;
            if (ring->state_flight.compare_exchange_weak(old, desired,
                    std::memory_order_acq_rel, std::memory_order_acquire))
            {
                break;
            }
        }

        // Wait for all admitted publishers to finish.
        bool quiesced = true;
        microseconds deadline{header_->commit_timeout_us};
        nanoseconds start = kickcat::since_epoch();
        while (ring::get_in_flight(
                   ring->state_flight.load(std::memory_order_acquire)) > 0)
        {
            if (kickcat::elapsed_time(start) >= deadline)
            {
                // Publisher likely crashed. Do NOT force in_flight to 0:
                // a slow-but-alive publisher may still be mid-commit.
                // Skip drain to avoid racing with it. Leaked slot refs
                // are recoverable by GC (reclaim_orphaned_slots).
                quiesced = false;
                ++drain_timeouts_;
                break;
            }
            kickcat::sleep(0ns);
        }

        if (quiesced)
        {
            drain_unconsumed(ring);
            // in_flight == 0 — safe to store directly.
            ring->state_flight.store(ring::make_packed(ring::Free),
                                     std::memory_order_release);
        }
        else
        {
            // Timeout: only change state bits, preserve in_flight
            // for the slow/crashed publisher.
            old = ring->state_flight.load(std::memory_order_acquire);
            while (true)
            {
                uint32_t desired = (old & ~ring::STATE_MASK) | ring::Free;
                if (ring->state_flight.compare_exchange_weak(old, desired,
                        std::memory_order_release,
                        std::memory_order_acquire))
                {
                    break;
                }
            }
        }

        ring_idx_ = UINT32_MAX;
    }

    Subscriber::~Subscriber()
    {
        release_ring();
    }

    Subscriber::Subscriber(Subscriber&& other) noexcept
        : base_{other.base_}
        , header_{other.header_}
        , ring_idx_{other.ring_idx_}
        , start_pos_{other.start_pos_}
        , read_pos_{other.read_pos_}
        , lost_{other.lost_}
        , drain_timeouts_{other.drain_timeouts_}
        , recv_buf_{std::move(other.recv_buf_)}
    {
        other.ring_idx_ = UINT32_MAX;
    }

    Subscriber& Subscriber::operator=(Subscriber&& other) noexcept
    {
        if (this != &other)
        {
            release_ring();

            base_            = other.base_;
            header_          = other.header_;
            ring_idx_        = other.ring_idx_;
            start_pos_       = other.start_pos_;
            read_pos_        = other.read_pos_;
            lost_            = other.lost_;
            drain_timeouts_ += other.drain_timeouts_;
            recv_buf_        = std::move(other.recv_buf_);

            other.ring_idx_ = UINT32_MAX;
        }
        return *this;
    }

    std::optional<Subscriber::SampleRef> Subscriber::try_receive()
    {
        auto* ring = sub_ring_at(base_, header_, ring_idx_);

        for (int retries = 0; retries < 64; ++retries)
        {
            uint64_t wp = ring->write_pos.load(std::memory_order_acquire);
            if (wp <= read_pos_)
            {
                return std::nullopt;
            }

            uint64_t capacity = header_->sub_ring_capacity;
            if (wp - read_pos_ > capacity)
            {
                lost_ += (wp - read_pos_) - capacity;
                read_pos_ = wp - capacity;
            }

            uint64_t idx  = read_pos_ & header_->sub_ring_mask;
            auto* entries = ring_entries(ring);
            auto& e       = entries[idx];

            // Acquire: ensures we see the slot_idx/payload_len written
            // by the publisher before the sequence commit.
            uint64_t seq1 = e.sequence.load(std::memory_order_acquire);
            if (seq1 != read_pos_ + 1)
            {
                if (seq1 == LOCKED_SEQUENCE or seq1 < read_pos_ + 1)
                {
                    // Publisher is mid-commit (LOCKED_SEQUENCE) or has not
                    // committed yet. Come back later.
                    return std::nullopt;
                }
                // Entry was overwritten (seq > expected): advance and retry.
                ++lost_;
                ++read_pos_;
                continue;
            }

            uint32_t slot_idx    = e.slot_idx.load(std::memory_order_relaxed);
            uint32_t payload_len = e.payload_len.load(std::memory_order_relaxed);

            if (slot_idx >= header_->pool_size or payload_len > header_->slot_data_size)
            {
                ++lost_;
                ++read_pos_;
                continue;
            }

            // Pin the slot via refcount increment to prevent it from being
            // freed while we memcpy. Without the pin, a publisher could evict
            // the ring entry and push the slot back to the free stack, letting
            // another publisher overwrite the data mid-copy.
            auto* slot = slot_at(base_, header_, slot_idx);
            uint32_t rc = slot->refcount.load(std::memory_order_acquire);
            bool pinned = false;
            while (rc > 0)
            {
                if (slot->refcount.compare_exchange_weak(rc, rc + 1,
                        std::memory_order_acq_rel, std::memory_order_acquire))
                {
                    pinned = true;
                    break;
                }
            }

            if (not pinned)
            {
                // refcount == 0: slot already freed, count as lost.
                ++lost_;
                ++read_pos_;
                continue;
            }

            // Seqlock validation: re-read the sequence after pinning. If it
            // changed, the entry was overwritten between our first read and
            // the pin, so the slot_idx we pinned may be stale.
            uint64_t seq2 = e.sequence.load(std::memory_order_acquire);
            if (seq2 != seq1)
            {
                uint32_t prev = slot->refcount.fetch_sub(1, std::memory_order_acq_rel);
                if (prev == 1)
                {
                    treiber_push(header_->free_top, slot, slot_idx);
                }
                ++lost_;
                ++read_pos_;
                continue;
            }

            std::memcpy(recv_buf_.data(), slot_data(slot), payload_len);

            // Unpin: we have our copy, release the slot reference.
            uint32_t prev = slot->refcount.fetch_sub(1, std::memory_order_acq_rel);
            if (prev == 1)
            {
                treiber_push(header_->free_top, slot, slot_idx);
            }

            ++read_pos_;
            return SampleRef{recv_buf_.data(), payload_len, read_pos_ - 1};
        }
        return std::nullopt;
    }

    std::optional<Subscriber::SampleRef> Subscriber::receive(nanoseconds timeout)
    {
        auto*       ring  = sub_ring_at(base_, header_, ring_idx_);
        nanoseconds start = kickcat::since_epoch();

        while (true)
        {
            auto sample = try_receive();
            if (sample)
            {
                return sample;
            }

            nanoseconds elapsed = kickcat::elapsed_time(start);
            if (elapsed >= timeout)
            {
                return std::nullopt;
            }

            uint64_t cur = ring->write_pos.load(std::memory_order_relaxed);
            if (cur <= read_pos_)
            {
                nanoseconds remaining = timeout - elapsed;
                if (remaining <= 0ns)
                {
                    return std::nullopt;
                }
                ring->has_waiter.store(1, std::memory_order_relaxed);
                futex_wait(ring->write_pos, cur, remaining);
                ring->has_waiter.store(0, std::memory_order_relaxed);
            }
        }
    }

    std::optional<Subscriber::SampleView> Subscriber::try_receive_view()
    {
        auto* ring = sub_ring_at(base_, header_, ring_idx_);

        for (int retries = 0; retries < 64; ++retries)
        {
            uint64_t wp = ring->write_pos.load(std::memory_order_acquire);
            if (wp <= read_pos_)
            {
                return std::nullopt;
            }

            uint64_t capacity = header_->sub_ring_capacity;
            if (wp - read_pos_ > capacity)
            {
                lost_ += (wp - read_pos_) - capacity;
                read_pos_ = wp - capacity;
            }

            uint64_t idx  = read_pos_ & header_->sub_ring_mask;
            auto* entries = ring_entries(ring);
            auto& e       = entries[idx];

            uint64_t seq1 = e.sequence.load(std::memory_order_acquire);
            if (seq1 != read_pos_ + 1)
            {
                if (seq1 == LOCKED_SEQUENCE or seq1 < read_pos_ + 1)
                {
                    return std::nullopt;
                }
                ++lost_;
                ++read_pos_;
                continue;
            }

            uint32_t slot_idx    = e.slot_idx.load(std::memory_order_relaxed);
            uint32_t payload_len = e.payload_len.load(std::memory_order_relaxed);

            if (slot_idx >= header_->pool_size or payload_len > header_->slot_data_size)
            {
                ++lost_;
                ++read_pos_;
                continue;
            }

            // Pin the slot so it survives until ~SampleView().
            auto* slot = slot_at(base_, header_, slot_idx);
            uint32_t rc = slot->refcount.load(std::memory_order_acquire);
            bool pinned = false;
            while (rc > 0)
            {
                if (slot->refcount.compare_exchange_weak(rc, rc + 1,
                        std::memory_order_acq_rel, std::memory_order_acquire))
                {
                    pinned = true;
                    break;
                }
            }

            if (not pinned)
            {
                ++lost_;
                ++read_pos_;
                continue;
            }

            // Seqlock validation after pinning.
            uint64_t seq2 = e.sequence.load(std::memory_order_acquire);
            if (seq2 != seq1)
            {
                uint32_t prev = slot->refcount.fetch_sub(1,
                                    std::memory_order_acq_rel);
                if (prev == 1)
                {
                    treiber_push(header_->free_top, slot, slot_idx);
                }
                ++lost_;
                ++read_pos_;
                continue;
            }

            ++read_pos_;
            return SampleView{base_, header_, slot_idx, payload_len, read_pos_ - 1};
        }
        return std::nullopt;
    }

    std::optional<Subscriber::SampleView> Subscriber::receive_view(nanoseconds timeout)
    {
        auto*       ring  = sub_ring_at(base_, header_, ring_idx_);
        nanoseconds start = kickcat::since_epoch();

        while (true)
        {
            auto sample = try_receive_view();
            if (sample)
            {
                return sample;
            }

            nanoseconds elapsed = kickcat::elapsed_time(start);
            if (elapsed >= timeout)
            {
                return std::nullopt;
            }

            uint64_t cur = ring->write_pos.load(std::memory_order_relaxed);
            if (cur <= read_pos_)
            {
                nanoseconds remaining = timeout - elapsed;
                if (remaining <= 0ns)
                {
                    return std::nullopt;
                }
                ring->has_waiter.store(1, std::memory_order_relaxed);
                futex_wait(ring->write_pos, cur, remaining);
                ring->has_waiter.store(0, std::memory_order_relaxed);
            }
        }
    }

    void Subscriber::drain_unconsumed(SubRingHeader* ring)
    {
        auto*    entries  = ring_entries(ring);
        uint64_t capacity = header_->sub_ring_capacity;

        // write_pos is final: the in_flight spin in the destructor guarantees
        // no publisher is mid-commit on this ring.
        uint64_t wp = ring->write_pos.load(std::memory_order_acquire);

        if (wp == 0)
        {
            return;
        }

        // Only release entries this subscriber is responsible for:
        // [max(oldest, start_pos_), wp). Entries before start_pos_ belong
        // to a previous subscriber on this ring slot and were already released.
        uint64_t oldest = 0;
        if (wp > capacity)
        {
            oldest = wp - capacity;
        }
        if (oldest < start_pos_)
        {
            oldest = start_pos_;
        }

        // Release this ring's reference for ALL committed entries in the live window:
        // - [oldest, read_pos_): consumed by try_receive (pin/unpin is net-zero,
        //   so the ring's original rc=1 reference still needs releasing).
        //   For try_receive_view, rc=2 (ring ref + SampleView pin); we release
        //   the ring ref here, ~SampleView releases the pin later.
        // - [read_pos_, wp): unconsumed entries, also need their ring ref released.
        // Evicted entries have seq != pos+1, so the check safely skips them.
        for (uint64_t pos = oldest; pos < wp; ++pos)
        {
            auto&    e   = entries[pos & header_->sub_ring_mask];
            uint64_t seq = e.sequence.load(std::memory_order_acquire);

            if (seq != pos + 1)
            {
                continue;
            }

            uint32_t slot_idx = e.slot_idx.load(std::memory_order_relaxed);
            if (slot_idx < header_->pool_size)
            {
                auto*    slot = slot_at(base_, header_, slot_idx);
                uint32_t prev = slot->refcount.fetch_sub(1,
                                    std::memory_order_acq_rel);
                if (prev == 1)
                {
                    treiber_push(header_->free_top, slot, slot_idx);
                }
                e.slot_idx.store(INVALID_SLOT, std::memory_order_seq_cst);
            }
        }

    }
} // namespace kickmsg
