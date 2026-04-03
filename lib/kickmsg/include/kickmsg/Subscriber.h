#ifndef KICKMSG_SUBSCRIBER_H
#define KICKMSG_SUBSCRIBER_H

#include <cstring>
#include <optional>
#include <stdexcept>
#include <vector>

#include "types.h"
#include "Region.h"
#include "kickcat/OS/Futex.h"

namespace kickmsg
{
    using kickcat::futex_wait;

    class Subscriber
    {
    public:
        // Copy-based sample: data is copied into subscriber-local memory.
        // Move-only: the internal buffer is reused across try_receive()
        // calls, so copies would alias the same memory.
        class SampleRef
        {
        public:
            SampleRef(void const* data, std::size_t len)
                : data_{data}
                , len_{len}
            {
            }

            ~SampleRef()
            {
#ifndef NDEBUG
                data_ = nullptr;
                len_  = 0;
#endif
            }

            SampleRef(SampleRef const&) = delete;
            SampleRef& operator=(SampleRef const&) = delete;

            SampleRef(SampleRef&& other) noexcept
                : data_{other.data_}
                , len_{other.len_}
            {
#ifndef NDEBUG
                other.data_ = nullptr;
                other.len_  = 0;
#endif
            }

            SampleRef& operator=(SampleRef&& other) noexcept
            {
                if (this != &other)
                {
                    data_ = other.data_;
                    len_  = other.len_;
#ifndef NDEBUG
                    other.data_ = nullptr;
                    other.len_  = 0;
#endif
                }
                return *this;
            }

            void const* data() const { return data_; }
            std::size_t len()  const { return len_; }

        private:
            void const* data_;
            std::size_t len_;
        };

        // Zero-copy sample: data points directly into shared memory.
        // Holds a refcount pin on the slot, released on destruction.
        // Must not outlive the SharedRegion.
        class SampleView
        {
        public:
            SampleView()
                : base_{nullptr}
                , header_{nullptr}
                , slot_idx_{INVALID_SLOT}
                , len_{0}
            {
            }

            ~SampleView() { release(); }

            SampleView(SampleView const&) = delete;
            SampleView& operator=(SampleView const&) = delete;

            SampleView(SampleView&& other) noexcept
                : base_{other.base_}
                , header_{other.header_}
                , slot_idx_{other.slot_idx_}
                , len_{other.len_}
            {
                other.slot_idx_ = INVALID_SLOT;
            }

            SampleView& operator=(SampleView&& other) noexcept
            {
                if (this != &other)
                {
                    release();
                    base_     = other.base_;
                    header_      = other.header_;
                    slot_idx_ = other.slot_idx_;
                    len_      = other.len_;
                    other.slot_idx_ = INVALID_SLOT;
                }
                return *this;
            }

            void const* data() const
            {
                return slot_data(slot_at(base_, header_, slot_idx_));
            }

            std::size_t len() const { return len_; }

        private:
            friend class Subscriber;

            SampleView(void* base, Header* hdr, uint32_t slot_idx, uint32_t len)
                : base_{base}
                , header_{hdr}
                , slot_idx_{slot_idx}
                , len_{len}
            {
            }

            void release()
            {
                if (slot_idx_ != INVALID_SLOT)
                {
                    auto* slot = slot_at(base_, header_, slot_idx_);
                    auto  prev = slot->refcount.fetch_sub(1,
                                     std::memory_order_acq_rel);
                    if (prev == 1)
                    {
                        treiber_push(header_->free_top, slot, slot_idx_);
                    }
                    slot_idx_ = INVALID_SLOT;
                }
            }

            void*    base_;
            Header*  header_;
            uint32_t slot_idx_;
            uint32_t len_;
        };

        explicit Subscriber(SharedRegion& region)
            : base_{region.base()}
            , header_{region.header()}
            , ring_idx_{UINT32_MAX}
            , read_pos_{0}
            , lost_{0}
        {
            recv_buf_.resize(header_->slot_data_size);

            for (uint32_t i = 0; i < header_->max_subs; ++i)
            {
                auto*    ring     = sub_ring_at(base_, header_, i);
                uint32_t expected = 0;
                if (ring->active.compare_exchange_strong(expected, 1,
                        std::memory_order_acq_rel))
                {
                    ring_idx_ = i;
                    read_pos_ = ring->write_pos.load(std::memory_order_acquire);
                    break;
                }
            }

            if (ring_idx_ == UINT32_MAX)
            {
                throw std::runtime_error("No free subscriber slots");
            }
        }

        ~Subscriber()
        {
            if (ring_idx_ != UINT32_MAX)
            {
                auto* ring = sub_ring_at(base_, header_, ring_idx_);
                ring->active.store(0, std::memory_order_release);
                drain_unconsumed(ring);
            }
        }

        Subscriber(Subscriber const&) = delete;
        Subscriber& operator=(Subscriber const&) = delete;

        Subscriber(Subscriber&& other) noexcept
            : base_{other.base_}
            , header_{other.header_}
            , ring_idx_{other.ring_idx_}
            , read_pos_{other.read_pos_}
            , lost_{other.lost_}
            , recv_buf_{std::move(other.recv_buf_)}
        {
            other.ring_idx_ = UINT32_MAX;
        }

        Subscriber& operator=(Subscriber&& other) noexcept
        {
            if (this != &other)
            {
                if (ring_idx_ != UINT32_MAX)
                {
                    auto* ring = sub_ring_at(base_, header_, ring_idx_);
                    ring->active.store(0, std::memory_order_release);
                    drain_unconsumed(ring);
                }

                base_     = other.base_;
                header_      = other.header_;
                ring_idx_ = other.ring_idx_;
                read_pos_ = other.read_pos_;
                lost_     = other.lost_;
                recv_buf_ = std::move(other.recv_buf_);

                other.ring_idx_ = UINT32_MAX;
            }
            return *this;
        }

        std::optional<SampleRef> try_receive()
        {
            auto* ring = sub_ring_at(base_, header_, ring_idx_);

            for (int retries = 0; retries < 64; ++retries)
            {
                auto wp = ring->write_pos.load(std::memory_order_acquire);
                if (wp <= read_pos_)
                {
                    return std::nullopt;
                }

                auto capacity = header_->sub_ring_capacity;
                if (wp - read_pos_ > capacity)
                {
                    lost_ += (wp - read_pos_) - capacity;
                    read_pos_ = wp - capacity;
                }

                auto  idx     = read_pos_ & header_->sub_ring_mask;
                auto* entries = ring_entries(ring);
                auto& e       = entries[idx];

                auto seq1 = e.sequence.load(std::memory_order_acquire);
                if (seq1 != read_pos_ + 1)
                {
                    if (seq1 > read_pos_ + 1)
                    {
                        ++lost_;
                        ++read_pos_;
                        continue;
                    }
                    return std::nullopt;
                }

                auto slot_idx    = e.slot_idx.load(std::memory_order_relaxed);
                auto payload_len = e.payload_len.load(std::memory_order_relaxed);

                if (slot_idx >= header_->pool_size or payload_len > header_->slot_data_size)
                {
                    ++lost_;
                    ++read_pos_;
                    continue;
                }

                auto* slot = slot_at(base_, header_, slot_idx);
                std::memcpy(recv_buf_.data(), slot_data(slot), payload_len);

                auto seq2 = e.sequence.load(std::memory_order_acquire);
                if (seq2 != seq1)
                {
                    ++lost_;
                    ++read_pos_;
                    continue;
                }

                ++read_pos_;
                return SampleRef{recv_buf_.data(), payload_len};
            }
            return std::nullopt;
        }

        std::optional<SampleRef> receive(nanoseconds timeout)
        {
            auto* ring     = sub_ring_at(base_, header_, ring_idx_);
            auto  deadline = steady_clock::now() + timeout;

            while (true)
            {
                auto sample = try_receive();
                if (sample)
                {
                    return sample;
                }

                if (steady_clock::now() >= deadline)
                {
                    return std::nullopt;
                }

                auto cur = ring->write_pos.load(std::memory_order_relaxed);
                if (cur <= read_pos_)
                {
                    auto remaining = deadline - steady_clock::now();
                    if (remaining <= nanoseconds::zero())
                    {
                        return std::nullopt;
                    }
                    futex_wait(ring->write_pos, cur, remaining);
                }
            }
        }

        std::optional<SampleView> try_receive_view()
        {
            auto* ring = sub_ring_at(base_, header_, ring_idx_);

            for (int retries = 0; retries < 64; ++retries)
            {
                auto wp = ring->write_pos.load(std::memory_order_acquire);
                if (wp <= read_pos_)
                {
                    return std::nullopt;
                }

                auto capacity = header_->sub_ring_capacity;
                if (wp - read_pos_ > capacity)
                {
                    lost_ += (wp - read_pos_) - capacity;
                    read_pos_ = wp - capacity;
                }

                auto  idx     = read_pos_ & header_->sub_ring_mask;
                auto* entries = ring_entries(ring);
                auto& e       = entries[idx];

                auto seq1 = e.sequence.load(std::memory_order_acquire);
                if (seq1 != read_pos_ + 1)
                {
                    if (seq1 > read_pos_ + 1)
                    {
                        ++lost_;
                        ++read_pos_;
                        continue;
                    }
                    return std::nullopt;
                }

                auto slot_idx    = e.slot_idx.load(std::memory_order_relaxed);
                auto payload_len = e.payload_len.load(std::memory_order_relaxed);

                if (slot_idx >= header_->pool_size or payload_len > header_->slot_data_size)
                {
                    ++lost_;
                    ++read_pos_;
                    continue;
                }

                auto* slot = slot_at(base_, header_, slot_idx);
                uint32_t rc = slot->refcount.load(std::memory_order_acquire);
                while (rc > 0)
                {
                    if (slot->refcount.compare_exchange_weak(rc, rc + 1,
                            std::memory_order_acq_rel, std::memory_order_acquire))
                    {
                        goto pinned;
                    }
                }
                ++lost_;
                ++read_pos_;
                continue;

            pinned:
                {
                    auto seq2 = e.sequence.load(std::memory_order_acquire);
                    if (seq2 != seq1)
                    {
                        auto prev = slot->refcount.fetch_sub(1,
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
            }
            return std::nullopt;
        }

        std::optional<SampleView> receive_view(nanoseconds timeout)
        {
            auto* ring     = sub_ring_at(base_, header_, ring_idx_);
            auto  deadline = steady_clock::now() + timeout;

            while (true)
            {
                auto sample = try_receive_view();
                if (sample)
                {
                    return sample;
                }

                if (steady_clock::now() >= deadline)
                {
                    return std::nullopt;
                }

                auto cur = ring->write_pos.load(std::memory_order_relaxed);
                if (cur <= read_pos_)
                {
                    auto remaining = deadline - steady_clock::now();
                    if (remaining <= nanoseconds::zero())
                    {
                        return std::nullopt;
                    }
                    futex_wait(ring->write_pos, cur, remaining);
                }
            }
        }

        uint64_t lost() const { return lost_; }

    private:
        void drain_unconsumed(SubRingHeader* ring)
        {
            auto wp       = ring->write_pos.load(std::memory_order_acquire);
            auto capacity = header_->sub_ring_capacity;

            if (wp <= read_pos_)
            {
                return;
            }

            if (wp - read_pos_ > capacity)
            {
                read_pos_ = wp - capacity;
            }

            auto* entries = ring_entries(ring);
            while (read_pos_ < wp)
            {
                auto  idx = read_pos_ & header_->sub_ring_mask;
                auto& e   = entries[idx];

                auto seq = e.sequence.load(std::memory_order_acquire);
                if (seq != read_pos_ + 1)
                {
                    ++read_pos_;
                    continue;
                }

                auto slot_idx = e.slot_idx.load(std::memory_order_relaxed);
                if (slot_idx < header_->pool_size)
                {
                    auto* slot = slot_at(base_, header_, slot_idx);
                    auto  prev = slot->refcount.fetch_sub(1,
                                     std::memory_order_acq_rel);
                    if (prev == 1)
                    {
                        treiber_push(header_->free_top, slot, slot_idx);
                    }
                }
                ++read_pos_;
            }
        }

        void*                base_;
        Header*              header_;
        uint32_t             ring_idx_;
        uint64_t             read_pos_;
        uint64_t             lost_;
        std::vector<uint8_t> recv_buf_;
    };

} // namespace kickmsg

#endif // KICKMSG_SUBSCRIBER_H
