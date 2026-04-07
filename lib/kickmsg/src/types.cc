#include "kickmsg/types.h"

namespace kickmsg
{
    SubRingHeader* sub_ring_at(void* base, Header const* h, uint32_t idx)
    {
        auto* p = static_cast<uint8_t*>(base) + h->sub_rings_offset;
        return reinterpret_cast<SubRingHeader*>(p + idx * h->sub_ring_stride);
    }

    Entry* ring_entries(SubRingHeader* ring)
    {
        return reinterpret_cast<Entry*>(
            reinterpret_cast<uint8_t*>(ring) + sizeof(SubRingHeader));
    }

    SlotHeader* slot_at(void* base, Header const* h, uint32_t idx)
    {
        auto* p = static_cast<uint8_t*>(base) + h->pool_offset;
        return reinterpret_cast<SlotHeader*>(p + idx * h->slot_stride);
    }

    SlotHeader* slot_at(void* pool_base, std::size_t slot_stride, uint32_t idx)
    {
        auto* p = static_cast<uint8_t*>(pool_base);
        return reinterpret_cast<SlotHeader*>(p + idx * slot_stride);
    }

    uint8_t* slot_data(SlotHeader* slot)
    {
        return reinterpret_cast<uint8_t*>(slot + 1);
    }

    char* header_creator_name(Header* h)
    {
        return reinterpret_cast<char*>(h) + sizeof(Header);
    }

    // FNV-1a hash of config fields, used to detect parameter mismatches
    // when opening an existing region.
    uint64_t compute_config_hash(ChannelType type, ChannelConfig const& cfg)
    {
        constexpr uint64_t FNV_OFFSET = 14695981039346656037ULL;
        constexpr uint64_t FNV_PRIME  = 1099511628211ULL;

        auto mix = [](uint64_t h, uint64_t val) -> uint64_t
        {
            auto const* p = reinterpret_cast<uint8_t const*>(&val);
            for (std::size_t i = 0; i < sizeof(val); ++i)
            {
                h ^= p[i];
                h *= FNV_PRIME;
            }
            return h;
        };

        uint64_t h = FNV_OFFSET;
        h = mix(h, static_cast<uint64_t>(type));
        h = mix(h, static_cast<uint64_t>(cfg.max_subscribers));
        h = mix(h, static_cast<uint64_t>(cfg.sub_ring_capacity));
        h = mix(h, static_cast<uint64_t>(cfg.pool_size));
        h = mix(h, static_cast<uint64_t>(cfg.max_payload_size));
        return h;
    }

    void treiber_push(std::atomic<uint64_t>& top, SlotHeader* slot, uint32_t slot_idx)
    {
        uint64_t old_top = top.load(std::memory_order_relaxed);
        uint64_t new_top;
        do
        {
            slot->next_free.store(tagged_idx(old_top), std::memory_order_relaxed);
            new_top = tagged_pack(tagged_gen(old_top) + 1, slot_idx);
        }
        // Release: ensures next_free write is visible to any thread that later pops this slot.
        while (not top.compare_exchange_weak(old_top, new_top, std::memory_order_release, std::memory_order_relaxed));
    }

    uint32_t treiber_pop(std::atomic<uint64_t>& top, void* base, Header const* h)
    {
        return treiber_pop(top, static_cast<uint8_t*>(base) + h->pool_offset, h->slot_stride);
    }

    void treiber_push(std::atomic<uint64_t>& top, void* pool_base, std::size_t slot_stride, uint32_t slot_idx)
    {
        treiber_push(top, slot_at(pool_base, slot_stride, slot_idx), slot_idx);
    }

    uint32_t treiber_pop(std::atomic<uint64_t>& top, void* pool_base, std::size_t slot_stride)
    {
        // Acquire: pairs with the release in push to see the pushed slot's next_free.
        uint64_t old_top = top.load(std::memory_order_acquire);
        while (tagged_idx(old_top) != INVALID_SLOT)
        {
            auto*    slot = slot_at(pool_base, slot_stride, tagged_idx(old_top));
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
