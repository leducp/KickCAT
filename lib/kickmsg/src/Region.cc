#include "kickmsg/Region.h"
#include "kickcat/OS/Time.h"

#include <unistd.h>

namespace kickmsg
{
    SharedRegion SharedRegion::create(char const* name, channel::Type type,
                                     channel::Config const& cfg,
                                     char const* creator_name)
    {
        if (type != channel::PubSub and type != channel::Broadcast)
        {
            throw std::runtime_error("Unsupported channel type");
        }
        if (not is_power_of_two(cfg.sub_ring_capacity))
        {
            throw std::runtime_error("sub_ring_capacity must be a power of 2");
        }

        uint16_t    creator_len     = static_cast<uint16_t>(std::strlen(creator_name));
        std::size_t header_size     = align_up(sizeof(Header) + creator_len, CACHE_LINE);

        std::size_t ring_stride     = align_up(
            sizeof(SubRingHeader) + cfg.sub_ring_capacity * sizeof(Entry), CACHE_LINE);
        std::size_t slot_stride     = align_up(sizeof(SlotHeader) + cfg.max_payload_size, CACHE_LINE);

        std::size_t sub_rings_offset = header_size;
        std::size_t pool_offset      = sub_rings_offset + cfg.max_subscribers * ring_stride;
        std::size_t total_size       = pool_offset + cfg.pool_size * slot_stride;

        SharedRegion region;
        region.name_ = name;
        region.shm_.create(name, total_size);

        std::memset(region.base(), 0, total_size);

        auto* h = region.header();
        h->version           = VERSION;
        h->channel_type      = type;
        h->total_size        = total_size;
        h->sub_rings_offset  = sub_rings_offset;
        h->pool_offset       = pool_offset;
        h->max_subs          = cfg.max_subscribers;
        h->sub_ring_capacity = cfg.sub_ring_capacity;
        h->sub_ring_mask     = cfg.sub_ring_capacity - 1;
        h->pool_size         = cfg.pool_size;
        h->slot_data_size    = cfg.max_payload_size;
        h->slot_stride       = slot_stride;
        h->sub_ring_stride   = ring_stride;
        h->commit_timeout_us = static_cast<uint64_t>(cfg.commit_timeout.count());
        h->config_hash       = compute_config_hash(type, cfg);
        h->creator_pid       = static_cast<uint64_t>(getpid());
        h->created_at_ns     = static_cast<uint64_t>(kickcat::since_epoch().count());
        h->creator_name_len  = creator_len;
        std::memcpy(header_creator_name(h), creator_name, creator_len);

        h->free_top = tagged_pack(0, INVALID_SLOT);

        for (uint32_t i = 0; i < cfg.pool_size; ++i)
        {
            auto* slot = slot_at(region.base(), h, i);
            slot->refcount = 0;
            treiber_push(h->free_top, slot, i);
        }

        for (uint32_t i = 0; i < cfg.max_subscribers; ++i)
        {
            auto* ring = sub_ring_at(region.base(), h, i);
            ring->state_flight = ring::make_packed(ring::Free);
            ring->write_pos    = 0;
            ring->has_waiter   = 0;
        }

        // Write magic LAST with release: create_or_open() polls magic with
        // acquire, so all preceding init stores are visible once magic == MAGIC.
        h->magic.store(MAGIC, std::memory_order_release);
        return region;
    }

    SharedRegion SharedRegion::open(char const* name)
    {
        SharedRegion region;
        region.name_ = name;
        region.shm_.open(name);

        auto* h = region.header();
        if (h->magic.load(std::memory_order_acquire) != MAGIC)
        {
            throw std::runtime_error("Invalid shared memory (bad magic)");
        }
        if (h->version != VERSION)
        {
            throw std::runtime_error("Version mismatch");
        }

        return region;
    }

    SharedRegion SharedRegion::create_or_open(char const* name, channel::Type type,
                                              channel::Config const& cfg,
                                              char const* creator_name)
    {
        uint16_t    creator_len     = static_cast<uint16_t>(std::strlen(creator_name));
        std::size_t header_size     = align_up(sizeof(Header) + creator_len, CACHE_LINE);
        std::size_t ring_stride     = align_up(
            sizeof(SubRingHeader) + cfg.sub_ring_capacity * sizeof(Entry), CACHE_LINE);
        std::size_t slot_stride     = align_up(sizeof(SlotHeader) + cfg.max_payload_size, CACHE_LINE);
        std::size_t sub_rings_offset = header_size;
        std::size_t pool_offset      = sub_rings_offset + cfg.max_subscribers * ring_stride;
        std::size_t total_size       = pool_offset + cfg.pool_size * slot_stride;

        SharedMemory probe;
        if (probe.try_create(name, total_size))
        {
            probe.close();
            return create(name, type, cfg, creator_name);
        }

        uint64_t expected_hash = compute_config_hash(type, cfg);

        for (int i = 0; i < 200; ++i)
        {
            try
            {
                SharedMemory shm;
                shm.open(name);

                auto* h = static_cast<Header*>(shm.address());
                if (h->magic.load(std::memory_order_acquire) == MAGIC and h->version == VERSION)
                {
                    if (h->config_hash != expected_hash)
                    {
                        throw std::runtime_error(
                            std::string{"Config mismatch on existing region: "} + name);
                    }
                    SharedRegion region;
                    region.name_ = name;
                    region.shm_  = std::move(shm);
                    return region;
                }

                shm.close();
            }
            catch (std::runtime_error const&)
            {
                throw;
            }
            catch (...)
            {
            }
            kickcat::sleep(10ms);
        }

        throw std::runtime_error(
            std::string{"Timed out waiting for region init: "} + name);
    }

    void SharedRegion::unlink()
    {
        if (not name_.empty())
        {
            SharedMemory::unlink(name_);
        }
    }

    SharedRegion::HealthReport SharedRegion::diagnose()
    {
        auto* b = base();
        auto* h = header();
        HealthReport report{};

        for (uint64_t i = 0; i < h->max_subs; ++i)
        {
            auto* ring    = sub_ring_at(b, h, static_cast<uint32_t>(i));
            auto* entries = ring_entries(ring);
            uint64_t wp   = ring->write_pos.load(std::memory_order_acquire);
            uint64_t cap  = h->sub_ring_capacity;

            uint64_t start = 0;
            if (wp > cap)
            {
                start = wp - cap;
            }
            for (uint64_t pos = start; pos < wp; ++pos)
            {
                auto& e = entries[pos & h->sub_ring_mask];
                if (e.sequence.load(std::memory_order_acquire) == LOCKED_SEQUENCE)
                {
                    ++report.locked_entries;
                }
            }

            uint32_t    packed    = ring->state_flight.load(std::memory_order_acquire);
            ring::State state     = ring::get_state(packed);
            uint32_t    in_flight = ring::get_in_flight(packed);

            if (state == ring::Live)
            {
                ++report.live_rings;
            }
            else if (state == ring::Free and in_flight > 0)
            {
                ++report.retired_rings;
            }
            else if (state == ring::Draining and in_flight > 0)
            {
                ++report.draining_rings;
            }
        }

        return report;
    }

    std::size_t SharedRegion::repair_locked_entries()
    {
        auto* b   = base();
        auto* h   = header();
        std::size_t repaired = 0;

        for (uint64_t i = 0; i < h->max_subs; ++i)
        {
            auto*    ring    = sub_ring_at(b, h, static_cast<uint32_t>(i));
            auto*    entries = ring_entries(ring);
            uint64_t wp      = ring->write_pos.load(std::memory_order_acquire);
            uint64_t cap     = h->sub_ring_capacity;

            uint64_t start = 0;
            if (wp > cap)
            {
                start = wp - cap;
            }
            for (uint64_t pos = start; pos < wp; ++pos)
            {
                auto&    e   = entries[pos & h->sub_ring_mask];
                uint64_t seq = e.sequence.load(std::memory_order_acquire);

                if (seq == LOCKED_SEQUENCE)
                {
                    // The crashed publisher may have written garbage into
                    // slot_idx/payload_len. Mark the entry as having no
                    // valid slot so subscribers skip it and future evictions
                    // don't release a stale index.
                    e.slot_idx.store(INVALID_SLOT, std::memory_order_relaxed);
                    e.payload_len.store(0, std::memory_order_relaxed);

                    // Commit with the sequence future publishers expect:
                    // pos + 1 (not prev_seq). A publisher wrapping to this
                    // position will CAS(pos + 1 → LOCKED), which now succeeds.
                    e.sequence.store(pos + 1, std::memory_order_release);
                    ++repaired;
                }
            }
        }

        return repaired;
    }

    std::size_t SharedRegion::reset_retired_rings()
    {
        auto* b = base();
        auto* h = header();
        std::size_t reset = 0;

        for (uint64_t i = 0; i < h->max_subs; ++i)
        {
            auto*    ring   = sub_ring_at(b, h, static_cast<uint32_t>(i));
            uint32_t packed = ring->state_flight.load(std::memory_order_acquire);

            if (ring::get_state(packed) == ring::Free
                and ring::get_in_flight(packed) > 0)
            {
                ring->state_flight.store(ring::make_packed(ring::Free),
                                         std::memory_order_release);
                ++reset;
            }
        }

        return reset;
    }

    std::size_t SharedRegion::reclaim_orphaned_slots()
    {
        auto* b = base();
        auto* h = header();

        // Build a set of all slot indices referenced by committed ring entries.
        std::vector<bool> referenced(h->pool_size, false);

        for (uint64_t i = 0; i < h->max_subs; ++i)
        {
            auto*    ring    = sub_ring_at(b, h, static_cast<uint32_t>(i));
            auto*    entries = ring_entries(ring);
            uint64_t wp      = ring->write_pos.load(std::memory_order_acquire);
            uint64_t cap     = h->sub_ring_capacity;

            uint64_t start = 0;
            if (wp > cap)
            {
                start = wp - cap;
            }
            for (uint64_t pos = start; pos < wp; ++pos)
            {
                auto&    e   = entries[pos & h->sub_ring_mask];
                uint64_t seq = e.sequence.load(std::memory_order_acquire);

                // Skip uncommitted and locked entries.
                if (seq >= pos + 1 and seq != LOCKED_SEQUENCE)
                {
                    uint32_t idx = e.slot_idx;
                    if (idx < h->pool_size)
                    {
                        referenced[idx] = true;
                    }
                }
            }
        }

        // Reclaim unreferenced slots with refcount > 0.
        std::size_t reclaimed = 0;
        for (uint64_t idx = 0; idx < h->pool_size; ++idx)
        {
            if (referenced[idx])
            {
                continue;
            }

            auto*    slot = slot_at(b, h, static_cast<uint32_t>(idx));
            uint32_t rc   = slot->refcount.load(std::memory_order_acquire);
            if (rc > 0)
            {
                slot->refcount.store(0, std::memory_order_release);
                treiber_push(h->free_top, slot, static_cast<uint32_t>(idx));
                ++reclaimed;
            }
        }

        return reclaimed;
    }
}
