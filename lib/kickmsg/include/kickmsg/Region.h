#ifndef KICKMSG_REGION_H
#define KICKMSG_REGION_H

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
                                   channel::Config const& cfg,
                                   char const* creator_name = "");

        static SharedRegion open(char const* name);

        static SharedRegion create_or_open(char const* name, channel::Type type,
                                           channel::Config const& cfg,
                                           char const* creator_name = "");

        void unlink();

        void*       base()       { return shm_.address(); }
        void const* base() const { return shm_.address(); }

        Header*       header()       { return static_cast<Header*>(shm_.address()); }
        Header const* header() const { return static_cast<Header const*>(shm_.address()); }

        channel::Type channel_type() const { return header()->channel_type; }

        /// Lightweight read-only health check. Safe under live traffic.
        /// Counts locked entries and ring states without any writes.
        ///
        /// Supervisor policy:
        ///  - locked_entries > 0: crash residue, call repair_locked_entries()
        ///  - retired_rings > 0: safe for reset_retired_rings() after
        ///    confirming the crashed publisher is gone
        ///  - draining_rings > 0: usually transient (subscriber tearing down),
        ///    persistent counts may indicate a stuck teardown
        ///  - live_rings: normal occupancy
        struct HealthReport
        {
            uint32_t locked_entries;   ///< Entries stuck at LOCKED_SEQUENCE
            uint32_t retired_rings;    ///< Free rings with stale in_flight > 0
            uint32_t draining_rings;   ///< Draining rings with in_flight > 0
            uint32_t live_rings;       ///< Active subscriber rings
        };
        HealthReport diagnose();

        /// Repair ring entries stuck at LOCKED_SEQUENCE (publisher crashed
        /// mid-commit). Commits the entry with INVALID_SLOT so future
        /// publishers can wrap past it.
        ///
        /// Safe to call under live traffic: the worst outcome is a benign
        /// double-store if a slow (but alive) publisher commits at the same
        /// time. Can be called freely on a health-check timer.
        /// Returns the number of entries repaired.
        std::size_t repair_locked_entries();

        /// Reset retired rings (Free | in_flight>0) so new subscribers can
        /// claim them. These rings were left stuck by a subscriber teardown
        /// that timed out on a crashed publisher's in_flight.
        ///
        /// Only safe after confirming the crashed publisher is gone.
        /// Unlike repair_locked_entries(), this is a deliberate post-crash
        /// action, not a routine maintenance call.
        /// Returns the number of rings reset.
        std::size_t reset_retired_rings();

        /// Reclaim orphaned slots (refcount > 0 but not referenced by any ring entry).
        /// These are caused by publisher crashes between allocate and publish, or by
        /// skipped drain on subscriber teardown timeout.
        ///
        /// NOT safe under live traffic. Call only when:
        ///  - all publishers are quiesced (a publisher between refcount pre-set
        ///    and ring push has rc > 0 with no ring entry yet), AND
        ///  - no outstanding SampleView exists (a view holds a refcount pin on
        ///    its slot without any ring entry reference; reclaiming it would free
        ///    memory still being read).
        /// Returns the number of slots reclaimed.
        std::size_t reclaim_orphaned_slots();

    private:
        SharedMemory shm_;
        std::string  name_;
    };
}

#endif
