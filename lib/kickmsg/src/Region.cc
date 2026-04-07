#include "kickmsg/Region.h"

#include <thread>
#include <unistd.h>

namespace kickmsg
{
    SharedRegion SharedRegion::create(char const* name, ChannelType type,
                                     ChannelConfig const& cfg,
                                     char const* creator_name)
    {
        if (type != ChannelType::PubSub and type != ChannelType::Broadcast)
        {
            throw std::runtime_error("Unsupported channel type");
        }
        if (not is_power_of_two(cfg.sub_ring_capacity))
        {
            throw std::runtime_error("sub_ring_capacity must be a power of 2");
        }

        auto creator_len = static_cast<uint16_t>(std::strlen(creator_name));
        auto header_size = align_up(sizeof(Header) + creator_len, CACHE_LINE);

        auto ring_stride = align_up(
            sizeof(SubRingHeader) + cfg.sub_ring_capacity * sizeof(Entry), CACHE_LINE);
        auto slot_stride = align_up(sizeof(SlotMeta) + cfg.max_payload_size, CACHE_LINE);

        auto sub_rings_offset = header_size;
        auto pool_offset      = sub_rings_offset + cfg.max_subscribers * ring_stride;
        auto total_size       = pool_offset + cfg.pool_size * slot_stride;

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
        h->created_at_ns     = static_cast<uint64_t>(
            duration_cast<nanoseconds>(
                system_clock::now().time_since_epoch()).count());
        h->creator_name_len  = creator_len;
        std::memcpy(header_creator_name(h), creator_name, creator_len);

        h->free_top.store(tagged_pack(0, INVALID_SLOT), std::memory_order_relaxed);

        for (uint32_t i = 0; i < cfg.pool_size; ++i)
        {
            auto* slot = slot_at(region.base(), h, i);
            slot->refcount.store(0, std::memory_order_relaxed);
            treiber_push(h->free_top, slot, i);
        }

        for (uint32_t i = 0; i < cfg.max_subscribers; ++i)
        {
            auto* ring = sub_ring_at(region.base(), h, i);
            ring->active.store(0, std::memory_order_relaxed);
            ring->in_flight.store(0, std::memory_order_relaxed);
            ring->write_pos.store(0, std::memory_order_relaxed);
        }

        // Write magic LAST — create_or_open() polls magic as the "init complete"
        // sentinel. The release fence ensures all preceding stores are visible
        // before magic becomes MAGIC.
        std::atomic_thread_fence(std::memory_order_release);
        h->magic = MAGIC;
        return region;
    }

    SharedRegion SharedRegion::open(char const* name)
    {
        SharedRegion region;
        region.name_ = name;
        region.shm_.open(name);

        auto* h = region.header();
        if (h->magic != MAGIC)
        {
            throw std::runtime_error("Invalid shared memory (bad magic)");
        }
        if (h->version != VERSION)
        {
            throw std::runtime_error("Version mismatch");
        }

        return region;
    }

    SharedRegion SharedRegion::create_or_open(char const* name, ChannelType type,
                                              ChannelConfig const& cfg,
                                              char const* creator_name)
    {
        auto creator_len = static_cast<uint16_t>(std::strlen(creator_name));
        auto header_size = align_up(sizeof(Header) + creator_len, CACHE_LINE);
        auto ring_stride = align_up(
            sizeof(SubRingHeader) + cfg.sub_ring_capacity * sizeof(Entry), CACHE_LINE);
        auto slot_stride = align_up(sizeof(SlotMeta) + cfg.max_payload_size, CACHE_LINE);
        auto sub_rings_offset = header_size;
        auto pool_offset      = sub_rings_offset + cfg.max_subscribers * ring_stride;
        auto total_size       = pool_offset + cfg.pool_size * slot_stride;

        SharedMemory probe;
        if (probe.try_create(name, total_size))
        {
            probe.close();
            return create(name, type, cfg, creator_name);
        }

        auto expected_hash = compute_config_hash(type, cfg);

        for (int i = 0; i < 200; ++i)
        {
            try
            {
                SharedMemory shm;
                shm.open(name);

                auto* h = static_cast<Header*>(shm.address());
                if (h->magic == MAGIC and h->version == VERSION)
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
            std::this_thread::sleep_for(milliseconds{10});
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

    std::size_t SharedRegion::repair_locked_entries()
    {
        auto* b   = base();
        auto* h   = header();
        std::size_t repaired = 0;

        for (uint64_t i = 0; i < h->max_subs; ++i)
        {
            auto* ring    = sub_ring_at(b, h, static_cast<uint32_t>(i));
            auto* entries = ring_entries(ring);
            auto  wp      = ring->write_pos.load(std::memory_order_acquire);
            auto  cap     = h->sub_ring_capacity;

            uint64_t start = (wp > cap) ? (wp - cap) : 0;
            for (uint64_t pos = start; pos < wp; ++pos)
            {
                auto& e   = entries[pos & h->sub_ring_mask];
                auto  seq = e.sequence.load(std::memory_order_acquire);

                if (seq == LOCKED_SEQUENCE)
                {
                    uint64_t prev_seq = (pos >= cap) ? (pos - cap + 1) : 0;
                    e.sequence.store(prev_seq, std::memory_order_release);
                    ++repaired;
                }
            }
        }

        return repaired;
    }

    std::size_t SharedRegion::reclaim_orphaned_slots()
    {
        auto* b = base();
        auto* h = header();

        // Build a set of all slot indices referenced by committed ring entries.
        std::vector<bool> referenced(h->pool_size, false);

        for (uint64_t i = 0; i < h->max_subs; ++i)
        {
            auto* ring    = sub_ring_at(b, h, static_cast<uint32_t>(i));
            auto* entries = ring_entries(ring);
            auto  wp      = ring->write_pos.load(std::memory_order_acquire);
            auto  cap     = h->sub_ring_capacity;

            uint64_t start = (wp > cap) ? (wp - cap) : 0;
            for (uint64_t pos = start; pos < wp; ++pos)
            {
                auto& e   = entries[pos & h->sub_ring_mask];
                auto  seq = e.sequence.load(std::memory_order_acquire);

                // Skip uncommitted and locked entries.
                if (seq >= pos + 1 and seq != LOCKED_SEQUENCE)
                {
                    auto idx = e.slot_idx.load(std::memory_order_relaxed);
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

            auto* slot = slot_at(b, h, static_cast<uint32_t>(idx));
            auto  rc   = slot->refcount.load(std::memory_order_acquire);
            if (rc > 0)
            {
                slot->refcount.store(0, std::memory_order_release);
                treiber_push(h->free_top, slot, static_cast<uint32_t>(idx));
                ++reclaimed;
            }
        }

        return reclaimed;
    }
} // namespace kickmsg
