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

        void* allocate(std::size_t len);
        std::size_t publish();
        bool send(void const* data, std::size_t len);

    private:
        static uint32_t wait_and_capture_slot(Entry& e, uint64_t expected_seq,
                                              microseconds timeout);
        void release_slot(uint32_t idx);

        void*        base_;
        Header*      header_;
        microseconds commit_timeout_;
        uint32_t     pending_slot_;
        uint32_t     pending_len_;
    };

} // namespace kickmsg

#endif // KICKMSG_PUBLISHER_H
