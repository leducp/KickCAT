#include "kickcat/Treiber.h"

namespace kickcat
{
    SlotMeta* slot_meta_at(void* pool_base, std::size_t slot_stride, uint32_t idx)
    {
        auto* p = static_cast<uint8_t*>(pool_base);
        return reinterpret_cast<SlotMeta*>(p + idx * slot_stride);
    }

    uint8_t* slot_data(SlotMeta* slot)
    {
        return reinterpret_cast<uint8_t*>(slot + 1);
    }

    void treiber_push(std::atomic<uint64_t>& top, void* pool_base, std::size_t slot_stride, uint32_t slot_idx)
    {
        auto* slot = slot_meta_at(pool_base, slot_stride, slot_idx);
        uint64_t old_top = top.load(std::memory_order_relaxed);
        uint64_t new_top;
        do
        {
            // Relaxed: only needs to be visible once the CAS on top publishes it.
            slot->next_free.store(tagged_idx(old_top), std::memory_order_relaxed);
            new_top = tagged_pack(tagged_gen(old_top) + 1, slot_idx);
        }
        // Release: ensures next_free write is visible to any thread that later pops this slot.
        // Failure is relaxed: old_top is reloaded by the CAS itself, no ordering needed.
        while (not top.compare_exchange_weak(old_top, new_top, std::memory_order_release, std::memory_order_relaxed));
    }

    uint32_t treiber_pop(std::atomic<uint64_t>& top, void* pool_base, std::size_t slot_stride)
    {
        // Acquire: pairs with the release in push to see the pushed slot's next_free.
        uint64_t old_top = top.load(std::memory_order_acquire);
        while (tagged_idx(old_top) != INVALID_SLOT)
        {
            auto*    slot = slot_meta_at(pool_base, slot_stride, tagged_idx(old_top));
            // Relaxed: safe because the acquire on top already synchronized with the push.
            uint32_t next = slot->next_free.load(std::memory_order_relaxed);
            uint64_t new_top = tagged_pack(tagged_gen(old_top) + 1, next);
            // Acq_rel: release publishes the new top, acquire synchronizes with the last push.
            // Failure is acquire: on retry we need to see the next_free written by whoever changed top.
            if (top.compare_exchange_weak(old_top, new_top, std::memory_order_acq_rel, std::memory_order_acquire))
            {
                return tagged_idx(old_top);
            }
        }
        return INVALID_SLOT;
    }
}
