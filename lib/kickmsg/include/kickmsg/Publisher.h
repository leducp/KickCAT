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

        ~Publisher();

        Publisher(Publisher const&) = delete;
        Publisher& operator=(Publisher const&) = delete;

        Publisher(Publisher&& other) noexcept
            : base_{other.base_}
            , header_{other.header_}
            , commit_timeout_{other.commit_timeout_}
            , pending_slot_{other.pending_slot_}
            , pending_len_{other.pending_len_}
            , dropped_{other.dropped_}
        {
            other.pending_slot_ = INVALID_SLOT;
        }

        Publisher& operator=(Publisher&& other) noexcept
        {
            if (this != &other)
            {
                release_pending();
                base_           = other.base_;
                header_         = other.header_;
                commit_timeout_ = other.commit_timeout_;
                pending_slot_   = other.pending_slot_;
                pending_len_    = other.pending_len_;
                dropped_        = other.dropped_;
                other.pending_slot_ = INVALID_SLOT;
            }
            return *this;
        }

        void* allocate(std::size_t len);
        std::size_t publish();

        /// Allocate, copy, and publish in one call.
        /// Returns bytes written on success, -EMSGSIZE if too large, -EAGAIN if pool exhausted.
        int32_t send(void const* data, std::size_t len);

        /// Number of per-ring delivery drops (CAS lock contention or pool exhaustion).
        uint64_t dropped() const { return dropped_; }

    private:
        static uint32_t wait_and_capture_slot(Entry& e, uint64_t expected_seq,
                                              microseconds timeout);
        void release_slot(uint32_t idx);
        void release_pending();

        void*        base_;
        Header*      header_;
        microseconds commit_timeout_;
        uint32_t     pending_slot_;
        uint32_t     pending_len_;
        uint64_t     dropped_{0};
    };

} // namespace kickmsg

#endif // KICKMSG_PUBLISHER_H
