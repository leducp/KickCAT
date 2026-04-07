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
    constexpr uint32_t    VERSION         = 2;
    constexpr uint32_t    INVALID_SLOT    = UINT32_MAX;
    constexpr uint64_t    LOCKED_SEQUENCE = UINT64_MAX;
    constexpr std::size_t CACHE_LINE      = 64;

    constexpr uint64_t DEFAULT_COMMIT_TIMEOUT_US = 100'000; // 100 ms

    enum class ChannelType : uint32_t
    {
        PubSub    = 1,
        Broadcast = 2,
    };

    struct RingConfig
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

    struct Header
    {
        uint64_t    magic;
        uint32_t    version;
        ChannelType channel_type;
        uint64_t    total_size;

        uint64_t    sub_rings_offset;
        uint64_t    pool_offset;

        uint64_t    max_subs;
        uint64_t    sub_ring_capacity;
        uint64_t    sub_ring_mask;
        uint64_t    pool_size;
        uint64_t    slot_data_size;
        uint64_t    slot_stride;
        uint64_t    sub_ring_stride;

        uint64_t    commit_timeout_us;
        uint64_t    config_hash;

        uint64_t    creator_pid;
        uint64_t    created_at_ns;

        uint16_t    creator_name_len;
        // creator_name bytes follow immediately after sizeof(Header)

        alignas(CACHE_LINE) std::atomic<uint64_t> free_top;
    };

    struct Entry
    {
        std::atomic<uint64_t> sequence;
        std::atomic<uint32_t> slot_idx;
        std::atomic<uint32_t> payload_len;
    };

    struct SubRingHeader
    {
        alignas(CACHE_LINE) std::atomic<uint32_t> active;
        alignas(CACHE_LINE) std::atomic<uint32_t> in_flight;
        alignas(CACHE_LINE) std::atomic<uint64_t> write_pos;
    };

    struct SlotMeta
    {
        std::atomic<uint32_t> refcount;
        std::atomic<uint32_t> next_free;
    };

    static_assert(sizeof(SlotMeta) == 8,
        "SlotMeta must be 8 bytes (two uint32_t atomics, no padding)");
    static_assert(sizeof(Entry) == 16,
        "Entry must be 16 bytes (one uint64_t + two uint32_t atomics, no padding)");

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
    SlotMeta*      slot_at(void* base, Header const* h, uint32_t idx);
    SlotMeta*      slot_at(void* pool_base, std::size_t slot_stride, uint32_t idx);
    uint8_t*       slot_data(SlotMeta* slot);
    char*          header_creator_name(Header* h);

    uint64_t compute_config_hash(ChannelType type, RingConfig const& cfg);

    void     treiber_push(std::atomic<uint64_t>& top, SlotMeta* slot, uint32_t slot_idx);
    void     treiber_push(std::atomic<uint64_t>& top, void* pool_base, std::size_t slot_stride, uint32_t slot_idx);
    uint32_t treiber_pop(std::atomic<uint64_t>& top, void* base, Header const* h);
    uint32_t treiber_pop(std::atomic<uint64_t>& top, void* pool_base, std::size_t slot_stride);

} // namespace kickmsg

#endif // KICKMSG_TYPES_H
