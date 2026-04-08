#ifndef KICKMSG_TYPES_H
#define KICKMSG_TYPES_H

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>

namespace kickmsg
{
    using namespace std::chrono;

    constexpr uint64_t    MAGIC           = 0x4B49434B4D534721ULL; // "KICKMSG!"
    constexpr uint32_t    VERSION         = 3;
    constexpr uint32_t    INVALID_SLOT    = UINT32_MAX;
    constexpr uint64_t    LOCKED_SEQUENCE = UINT64_MAX;
    constexpr std::size_t CACHE_LINE      = 64;

    constexpr uint64_t DEFAULT_COMMIT_TIMEOUT_US = 100'000; // 100 ms

    namespace channel
    {
        enum Type : uint32_t
        {
            PubSub    = 1,
            Broadcast = 2,
        };
    }

    struct ChannelConfig
    {
        std::size_t max_subscribers   = 16;
        std::size_t sub_ring_capacity = 64;
        std::size_t pool_size         = 256;
        std::size_t max_payload_size  = 4096;

        // Maximum time a publisher waits for a previous writer to commit
        // before assuming it crashed.  Shorter = faster crash recovery but
        // higher risk of falsely evicting a slow-but-alive publisher under
        // heavy scheduling pressure.  Longer = safer under load but adds
        // latency when a real crash occurs.
        microseconds commit_timeout{DEFAULT_COMMIT_TIMEOUT_US};
    };

    // ---- Shared-memory layout structures ----

    /// Shared-memory region header. Written once by the creator, read by all.
    /// Layout version changes require a VERSION bump.
    struct Header
    {
        std::atomic<uint64_t> magic;    ///< MAGIC sentinel — written last (release) during init, polled (acquire) by create_or_open
        uint32_t    version;            ///< Layout version — rejects mismatched builds
        channel::Type channel_type;     ///< PubSub or Broadcast

        uint64_t    total_size;         ///< Total shared-memory region size in bytes

        uint64_t    sub_rings_offset;   ///< Byte offset from base to subscriber rings array
        uint64_t    pool_offset;        ///< Byte offset from base to slot pool

        uint64_t    max_subs;           ///< Maximum number of subscriber rings
        uint64_t    sub_ring_capacity;  ///< Entries per subscriber ring (power of 2)
        uint64_t    sub_ring_mask;      ///< sub_ring_capacity - 1 (for fast index masking)
        uint64_t    pool_size;          ///< Number of slots in the pool
        uint64_t    slot_data_size;     ///< Max payload bytes per slot
        uint64_t    slot_stride;        ///< Bytes between consecutive slots (aligned)
        uint64_t    sub_ring_stride;    ///< Bytes between consecutive subscriber rings (aligned)

        uint64_t    commit_timeout_us;  ///< Max wait for a previous writer to commit (crash detection)
        uint64_t    config_hash;        ///< FNV-1a of config fields — detects parameter mismatches on open

        uint64_t    creator_pid;        ///< PID of the process that created the region (debug)
        uint64_t    created_at_ns;      ///< Creation timestamp in nanoseconds since epoch (debug)

        uint16_t    creator_name_len;   ///< Length of creator name string
        // creator_name bytes follow immediately after sizeof(Header)

        alignas(CACHE_LINE) std::atomic<uint64_t> free_top; ///< Treiber free-stack head (tagged: gen|idx)
    };

    /// Ring entry: one per position in a subscriber ring.
    /// Packed to guarantee binary layout across compilers.
    struct Entry
    {
        std::atomic<uint64_t> sequence;     ///< Commit barrier (pos + 1) and seqlock for data consistency
        std::atomic<uint32_t> slot_idx;     ///< Index into the slot pool (INVALID_SLOT if released by drain)
        std::atomic<uint32_t> payload_len;  ///< Actual payload bytes written to the slot
    };

    /// Ring state machine for subscriber lifecycle.
    /// Free → Live (subscriber joins) → Draining (subscriber leaving) → Free
    namespace ring
    {
        enum State : uint32_t
        {
            Free     = 0,  ///< No subscriber — available for claim
            Live     = 1,  ///< Subscriber owns ring, publishers may deliver
            Draining = 2,  ///< Subscriber tearing down — no new delivery, drain in progress
        };
    }

    /// Per-subscriber ring header in shared memory.
    /// Fields are cache-line aligned to avoid false sharing between
    /// publisher (writes write_pos, reads state/in_flight) and
    /// subscriber (writes state, reads write_pos).
    struct SubRingHeader
    {
        alignas(CACHE_LINE) std::atomic<ring::State> state;   ///< Ring lifecycle state
        alignas(CACHE_LINE) std::atomic<uint32_t> in_flight; ///< Publishers currently admitted to this ring
        alignas(CACHE_LINE) std::atomic<uint64_t> write_pos; ///< Monotonically increasing position counter
    };

    /// Slot header: prepended to each payload buffer in the pool.
    /// Packed to guarantee binary layout across compilers.
    struct SlotHeader
    {
        std::atomic<uint32_t> refcount;  ///< Number of ring references + SampleView pins
        std::atomic<uint32_t> next_free; ///< Next slot index in the Treiber free-stack chain
    };

    static_assert(std::atomic<uint64_t>::is_always_lock_free,
        "KickMsg requires lock-free 64-bit atomics. "
        "32-bit platforms (RV32, MIPS32) are not supported.");
    static_assert(std::atomic<uint32_t>::is_always_lock_free,
        "KickMsg requires lock-free 32-bit atomics.");


    // ---- constexpr helpers (stay in header) ----

    constexpr std::size_t align_up(std::size_t val, std::size_t alignment)
    {
        return (val + alignment - 1) & ~(alignment - 1);
    }

    constexpr bool is_power_of_two(std::size_t n)
    {
        return n > 0 and (n & (n - 1)) == 0;
    }

    // ---- ABA-safe Treiber free-stack (lock-free) ----
    // Tagged pointer: high 32 bits = generation counter, low 32 bits = slot index.

    constexpr uint64_t tagged_pack(uint32_t gen, uint32_t idx)
    {
        return (static_cast<uint64_t>(gen) << 32) | idx;
    }

    constexpr uint32_t tagged_idx(uint64_t tagged) { return static_cast<uint32_t>(tagged); }
    constexpr uint32_t tagged_gen(uint64_t tagged) { return static_cast<uint32_t>(tagged >> 32); }

    SubRingHeader* sub_ring_at(void* base, Header const* h, uint32_t idx);
    Entry*         ring_entries(SubRingHeader* ring);
    SlotHeader*    slot_at(void* base, Header const* h, uint32_t idx);
    SlotHeader*    slot_at(void* pool_base, std::size_t slot_stride, uint32_t idx);
    uint8_t*       slot_data(SlotHeader* slot);
    char*          header_creator_name(Header* h);

    uint64_t compute_config_hash(channel::Type type, ChannelConfig const& cfg);

    void     treiber_push(std::atomic<uint64_t>& top, SlotHeader* slot, uint32_t slot_idx);
    void     treiber_push(std::atomic<uint64_t>& top, void* pool_base, std::size_t slot_stride, uint32_t slot_idx);
    uint32_t treiber_pop(std::atomic<uint64_t>& top, void* base, Header const* h);
    uint32_t treiber_pop(std::atomic<uint64_t>& top, void* pool_base, std::size_t slot_stride);

} // namespace kickmsg

#endif // KICKMSG_TYPES_H
