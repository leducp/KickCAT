#ifndef KICKCAT_TREIBER_H
#define KICKCAT_TREIBER_H

#include <atomic>
#include <cstdint>

namespace kickcat
{
    constexpr uint32_t    INVALID_SLOT = UINT32_MAX;
    constexpr std::size_t CACHE_LINE   = 64;

    // ---- ABA-safe Treiber free-stack (lock-free) ----
    // Tagged pointer: high 32 bits = generation counter, low 32 bits = slot index.

    constexpr uint64_t tagged_pack(uint32_t gen, uint32_t idx)
    {
        return (static_cast<uint64_t>(gen) << 32) | idx;
    }

    constexpr uint32_t tagged_idx(uint64_t tagged) { return static_cast<uint32_t>(tagged); }
    constexpr uint32_t tagged_gen(uint64_t tagged) { return static_cast<uint32_t>(tagged >> 32); }

    struct SlotMeta
    {
        std::atomic<uint32_t> next_free;
        uint32_t              pad;
    };

    static_assert(sizeof(SlotMeta) == 8,
        "SlotMeta must be 8 bytes (one atomic uint32_t + padding)");

    SlotMeta* slot_meta_at(void* pool_base, std::size_t slot_stride, uint32_t idx);
    uint8_t*  slot_data(SlotMeta* slot);

    constexpr std::size_t align_up(std::size_t val, std::size_t alignment)
    {
        return (val + alignment - 1) & ~(alignment - 1);
    }

    void treiber_push(std::atomic<uint64_t>& top, void* pool_base, std::size_t slot_stride, uint32_t slot_idx);

    /// Returns INVALID_SLOT if the stack is empty.
    uint32_t treiber_pop(std::atomic<uint64_t>& top, void* pool_base, std::size_t slot_stride);
}

#endif
