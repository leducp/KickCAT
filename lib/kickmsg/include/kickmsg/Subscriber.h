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
                data_ = nullptr;
                len_  = 0;
            }

            SampleRef(SampleRef const&) = delete;
            SampleRef& operator=(SampleRef const&) = delete;

            SampleRef(SampleRef&& other) noexcept
                : data_{other.data_}
                , len_{other.len_}
            {
                other.data_ = nullptr;
                other.len_  = 0;
            }

            SampleRef& operator=(SampleRef&& other) noexcept
            {
                if (this != &other)
                {
                    data_ = other.data_;
                    len_  = other.len_;
                    other.data_ = nullptr;
                    other.len_  = 0;
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

        Subscriber(SharedRegion& region);
        ~Subscriber();

        Subscriber(Subscriber const&) = delete;
        Subscriber& operator=(Subscriber const&) = delete;

        Subscriber(Subscriber&& other) noexcept;
        Subscriber& operator=(Subscriber&& other) noexcept;

        std::optional<SampleRef> try_receive();
        std::optional<SampleRef> receive(nanoseconds timeout);
        std::optional<SampleView> try_receive_view();
        std::optional<SampleView> receive_view(nanoseconds timeout);

        uint64_t lost() const { return lost_; }

    private:
        void drain_unconsumed(SubRingHeader* ring);

        void*                base_;
        Header*              header_;
        uint32_t             ring_idx_;
        uint64_t             start_pos_;
        uint64_t             read_pos_;
        uint64_t             lost_;
        std::vector<uint8_t> recv_buf_;
    };
}

#endif
