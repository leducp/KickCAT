#ifndef KICKMSG_REGION_H
#define KICKMSG_REGION_H

#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "types.h"
#include "kickcat/OS/SharedMemory.h"

namespace kickmsg
{
    using kickcat::SharedMemory;

    class SharedRegion
    {
    public:
        SharedRegion() = default;

        SharedRegion(SharedRegion const&) = delete;
        SharedRegion& operator=(SharedRegion const&) = delete;
        SharedRegion(SharedRegion&&) noexcept = default;
        SharedRegion& operator=(SharedRegion&&) noexcept = default;
        ~SharedRegion() = default;

        static SharedRegion create(char const* name, channel::Type type,
                                   ChannelConfig const& cfg,
                                   char const* creator_name = "");

        static SharedRegion open(char const* name);

        static SharedRegion create_or_open(char const* name, channel::Type type,
                                           ChannelConfig const& cfg,
                                           char const* creator_name = "");

        void unlink();

        void*       base()       { return shm_.address(); }
        void const* base() const { return shm_.address(); }

        Header*       header()       { return static_cast<Header*>(shm_.address()); }
        Header const* header() const { return static_cast<Header const*>(shm_.address()); }

        channel::Type channel_type() const { return header()->channel_type; }

        /// Repair ring entries stuck at LOCKED_SEQUENCE (publisher crashed mid-commit).
        /// Rolls the sequence back to the expected previous value, unblocking future
        /// publishers at that position.
        ///
        /// Safe to call under live traffic: the worst outcome is a benign double-store
        /// if a slow (but alive) publisher commits at the same time.
        /// Returns the number of entries repaired.
        std::size_t repair_locked_entries();

        /// Reclaim orphaned slots (refcount > 0 but not referenced by any ring entry).
        /// These are caused by publisher crashes between allocate and publish, or by
        /// the drain_unconsumed race on subscriber shutdown.
        ///
        /// NOT safe under live traffic: a publisher between refcount pre-set and ring
        /// push has rc > 0 with no ring entry yet. Reclaiming it causes silent data
        /// corruption. Call only when all publishers are quiesced.
        /// Returns the number of slots reclaimed.
        std::size_t reclaim_orphaned_slots();

    private:
        SharedMemory shm_;
        std::string  name_;
    };
}

#endif
