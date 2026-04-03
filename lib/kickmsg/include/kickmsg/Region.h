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

        static SharedRegion create(char const* name, ChannelType type,
                                   RingConfig const& cfg,
                                   char const* creator_name = "");

        static SharedRegion open(char const* name);

        static SharedRegion create_or_open(char const* name, ChannelType type,
                                           RingConfig const& cfg,
                                           char const* creator_name = "");

        void unlink();

        void*       base()       { return shm_.address(); }
        void const* base() const { return shm_.address(); }

        Header*       header()       { return static_cast<Header*>(shm_.address()); }
        Header const* header() const { return static_cast<Header const*>(shm_.address()); }

        ChannelType channel_type() const { return header()->channel_type; }

        /// Reclaim leaked slots and repair poisoned ring entries.
        ///
        /// Leaked slots: refcount > 0 but no ring entry references them (caused by
        /// publisher crash between allocate and publish, or drain_unconsumed race).
        ///
        /// Poisoned entries: sequence stuck at LOCKED_SEQUENCE because a publisher
        /// crashed while holding the two-phase commit lock. These are repaired by
        /// rolling the sequence back to the expected previous value, unblocking
        /// future publishers.
        ///
        /// Must be called from a single thread while no publishers are active,
        /// or accepted as a best-effort scan during operation.
        std::size_t collect_garbage();

    private:
        SharedMemory shm_;
        std::string  name_;
    };

} // namespace kickmsg

#endif // KICKMSG_REGION_H
