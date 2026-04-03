#ifndef KICKMSG_REGION_H
#define KICKMSG_REGION_H

#include <chrono>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

#include "types.h"
#include "OS/SharedMemory.h"

namespace kickmsg
{

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
                                   char const* creator_name = "")
        {
            if (type != ChannelType::PubSub && type != ChannelType::Broadcast)
            {
                throw std::runtime_error("Unsupported channel type");
            }
            if (!is_power_of_two(cfg.sub_ring_capacity))
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

            auto* hdr = region.header();
            hdr->magic             = MAGIC;
            hdr->version           = VERSION;
            hdr->channel_type      = type;
            hdr->total_size        = total_size;
            hdr->sub_rings_offset  = sub_rings_offset;
            hdr->pool_offset       = pool_offset;
            hdr->max_subs          = cfg.max_subscribers;
            hdr->sub_ring_capacity = cfg.sub_ring_capacity;
            hdr->sub_ring_mask     = cfg.sub_ring_capacity - 1;
            hdr->pool_size         = cfg.pool_size;
            hdr->slot_data_size    = cfg.max_payload_size;
            hdr->slot_stride       = slot_stride;
            hdr->sub_ring_stride   = ring_stride;
            hdr->commit_timeout_us = static_cast<uint64_t>(cfg.commit_timeout.count());
            hdr->config_hash       = compute_config_hash(type, cfg);
            hdr->creator_pid       = static_cast<uint64_t>(getpid());
            hdr->created_at_ns     = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count());
            hdr->creator_name_len  = creator_len;
            std::memcpy(header_creator_name(hdr), creator_name, creator_len);

            hdr->free_top.store(tagged_pack(0, INVALID_SLOT), std::memory_order_relaxed);

            for (uint32_t i = 0; i < cfg.pool_size; ++i)
            {
                auto* slot = slot_at(region.base(), hdr, i);
                slot->refcount.store(0, std::memory_order_relaxed);
                treiber_push(hdr->free_top, slot, i);
            }

            for (uint32_t i = 0; i < cfg.max_subscribers; ++i)
            {
                auto* ring = sub_ring_at(region.base(), hdr, i);
                ring->active.store(0, std::memory_order_relaxed);
                ring->write_pos.store(0, std::memory_order_relaxed);
            }

            std::atomic_thread_fence(std::memory_order_release);
            return region;
        }

        static SharedRegion open(char const* name)
        {
            SharedRegion region;
            region.name_ = name;
            region.shm_.open(name);

            auto* hdr = region.header();
            if (hdr->magic != MAGIC)
            {
                throw std::runtime_error("Invalid shared memory (bad magic)");
            }
            if (hdr->version != VERSION)
            {
                throw std::runtime_error("Version mismatch");
            }

            return region;
        }

        static SharedRegion create_or_open(char const* name, ChannelType type,
                                           RingConfig const& cfg,
                                           char const* creator_name = "")
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

                    auto* hdr = static_cast<Header*>(shm.address());
                    if (hdr->magic == MAGIC && hdr->version == VERSION)
                    {
                        if (hdr->config_hash != expected_hash)
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
                std::this_thread::sleep_for(std::chrono::milliseconds{10});
            }

            throw std::runtime_error(
                std::string{"Timed out waiting for region init: "} + name);
        }

        void unlink()
        {
            if (!name_.empty())
            {
                SharedMemory::unlink(name_);
            }
        }

        void*       base()       { return shm_.address(); }
        void const* base() const { return shm_.address(); }

        Header*       header()       { return static_cast<Header*>(shm_.address()); }
        Header const* header() const { return static_cast<Header const*>(shm_.address()); }

        ChannelType channel_type() const { return header()->channel_type; }

        std::size_t collect_garbage()
        {
            auto* b   = base();
            auto* hdr = header();

            std::vector<bool> referenced(hdr->pool_size, false);

            for (uint64_t i = 0; i < hdr->max_subs; ++i)
            {
                auto* ring    = sub_ring_at(b, hdr, static_cast<uint32_t>(i));
                auto* entries = ring_entries(ring);
                auto  wp      = ring->write_pos.load(std::memory_order_acquire);
                auto  cap     = hdr->sub_ring_capacity;

                uint64_t start = (wp > cap) ? (wp - cap) : 0;
                for (uint64_t pos = start; pos < wp; ++pos)
                {
                    auto& e   = entries[pos & hdr->sub_ring_mask];
                    auto  seq = e.sequence.load(std::memory_order_acquire);
                    if (seq >= pos + 1)
                    {
                        auto idx = e.slot_idx.load(std::memory_order_relaxed);
                        if (idx < hdr->pool_size)
                        {
                            referenced[idx] = true;
                        }
                    }
                }
            }

            std::size_t reclaimed = 0;
            for (uint64_t idx = 0; idx < hdr->pool_size; ++idx)
            {
                if (referenced[idx])
                {
                    continue;
                }

                auto* slot = slot_at(b, hdr, static_cast<uint32_t>(idx));
                auto  rc   = slot->refcount.load(std::memory_order_acquire);
                if (rc > 0)
                {
                    slot->refcount.store(0, std::memory_order_release);
                    treiber_push(hdr->free_top, slot, static_cast<uint32_t>(idx));
                    ++reclaimed;
                }
            }

            return reclaimed;
        }

    private:
        SharedMemory shm_;
        std::string  name_;
    };

} // namespace kickmsg

#endif // KICKMSG_REGION_H
