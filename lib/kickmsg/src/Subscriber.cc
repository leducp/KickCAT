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
            ring::State expected = ring::Free;
            if (ring->state.compare_exchange_strong(expected, ring::Live,
                    std::memory_order_seq_cst))
            {
                ring_idx_  = i;
                start_pos_ = ring->write_pos.load(std::memory_order_acquire);
                read_pos_  = start_pos_;
                break;
            }
        }

        if (ring_idx_ == UINT32_MAX)
        {
            throw std::runtime_error("No free subscriber slots");
        }
    }

    Subscriber::~Subscriber()
    {
        if (ring_idx_ != UINT32_MAX)
        {
            auto* ring = sub_ring_at(base_, header_, ring_idx_);
            // Transition Live → Draining: no new publisher can be admitted.
            ring->state.store(ring::Draining, std::memory_order_seq_cst);

            // Wait for all admitted publishers to finish.
            while (ring->in_flight.load(std::memory_order_seq_cst) > 0)
            {
                kickcat::sleep(0ns);
            }

            drain_unconsumed(ring);

            // Transition Draining → Free: ring available for a new subscriber.
            ring->state.store(ring::Free, std::memory_order_seq_cst);
        }
    }

    Subscriber::Subscriber(Subscriber&& other) noexcept
        : base_{other.base_}
        , header_{other.header_}
        , ring_idx_{other.ring_idx_}
        , start_pos_{other.start_pos_}
        , read_pos_{other.read_pos_}
        , lost_{other.lost_}
        , recv_buf_{std::move(other.recv_buf_)}
    {
        other.ring_idx_ = UINT32_MAX;
    }

    Subscriber& Subscriber::operator=(Subscriber&& other) noexcept
    {
        if (this != &other)
        {
            if (ring_idx_ != UINT32_MAX)
            {
                auto* ring = sub_ring_at(base_, header_, ring_idx_);
                ring->state.store(ring::Draining, std::memory_order_seq_cst);
                while (ring->in_flight.load(std::memory_order_seq_cst) > 0)
                {
                    kickcat::sleep(0ns);
                }
                drain_unconsumed(ring);
                ring->state.store(ring::Free, std::memory_order_seq_cst);
            }

            base_      = other.base_;
            header_    = other.header_;
            ring_idx_  = other.ring_idx_;
            start_pos_ = other.start_pos_;
            read_pos_  = other.read_pos_;
            lost_      = other.lost_;
            recv_buf_ = std::move(other.recv_buf_);

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
            return SampleRef{recv_buf_.data(), payload_len};
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
                futex_wait(ring->write_pos, cur, remaining);
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
            return SampleView{base_, header_, slot_idx, payload_len};
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
                futex_wait(ring->write_pos, cur, remaining);
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
                // Mark entry as released so a future publisher wrapping to this
                // position won't double-decrement via release_slot.
                // seq_cst: the release/acquire chain through active and in_flight
                // does not formally guarantee the publisher sees this store.
                // seq_cst establishes a total order visible to all threads.
                // Cost: one full barrier per drained entry, only during destruction.
                e.slot_idx.store(INVALID_SLOT, std::memory_order_seq_cst);
            }
        }
    }
} // namespace kickmsg
