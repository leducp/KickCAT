#ifndef KICKMSG_STRESS_COMMON_H
#define KICKMSG_STRESS_COMMON_H

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#include "kickcat/OS/Time.h"

#include "kickmsg/Publisher.h"
#include "kickmsg/Subscriber.h"

using namespace kickcat;

#if defined(__SANITIZE_THREAD__) || defined(__has_feature) && __has_feature(thread_sanitizer)
    constexpr int TSAN_SCALE = 100;
#else
    constexpr int TSAN_SCALE = 1;
#endif

struct Payload
{
    static constexpr uint32_t MAGIC = 0xCAFEBABE;
    uint32_t magic;
    uint32_t pub_id;
    uint32_t seq;
    uint32_t checksum;
};

inline uint32_t compute_checksum(Payload const& p)
{
    return p.magic ^ p.pub_id ^ p.seq ^ 0xDEADBEEF;
}

struct TestConfig
{
    int      num_publishers   = 4;
    int      num_subscribers  = 8;
    uint32_t msgs_per_pub     = 50000;
    std::size_t pool_size     = 512;
    std::size_t ring_capacity = 128;
    std::size_t max_subs      = 16;
    bool     use_zerocopy     = false;
};

struct SubResult
{
    int      sub_id;
    uint64_t received;
    uint64_t lost;
    uint64_t corrupted;
    uint64_t bad_pub_id;
    uint64_t reordered;
};

struct MsgTrace
{
    uint32_t pub_id;
    uint32_t seq;
    uint64_t ring_pos;
};

static constexpr std::size_t TRACE_SIZE = 16;

inline std::atomic<bool> g_all_publishers_done{false};

inline void publisher_thread(kickmsg::SharedRegion& region, int pub_id, uint32_t count)
{
    kickmsg::Publisher pub{region};

    for (uint32_t i = 0; i < count; ++i)
    {
        Payload msg;
        msg.magic  = Payload::MAGIC;
        msg.pub_id = static_cast<uint32_t>(pub_id);
        msg.seq    = i;
        msg.checksum = compute_checksum(msg);

        int32_t rc;
        while ((rc = pub.send(&msg, sizeof(msg))) < 0)
        {
            if (rc != -EAGAIN)
            {
                std::fprintf(stderr, "  [FATAL] publisher %d: send() returned %d\n", pub_id, rc);
                std::abort();
            }
            kickcat::sleep(0ns);
        }
    }
}

inline void validate_payload(Payload const& msg, int num_pubs, uint64_t ring_pos,
                             std::vector<uint32_t>& last_seq,
                             std::vector<uint64_t>& last_pos,
                             SubResult& result,
                             MsgTrace* trace, std::size_t& trace_pos)
{
    if (msg.magic != Payload::MAGIC)
    {
        ++result.corrupted;
        return;
    }
    if (msg.checksum != compute_checksum(msg))
    {
        ++result.corrupted;
        return;
    }
    if (msg.pub_id >= static_cast<uint32_t>(num_pubs))
    {
        ++result.bad_pub_id;
        return;
    }

    trace[trace_pos % TRACE_SIZE] = {msg.pub_id, msg.seq, ring_pos};
    ++trace_pos;

    auto& prev_seq = last_seq[msg.pub_id];
    auto& prev_pos = last_pos[msg.pub_id];

    if (prev_seq != UINT32_MAX and msg.seq <= prev_seq)
    {
        auto delta = static_cast<int32_t>(prev_seq) - static_cast<int32_t>(msg.seq);
        std::fprintf(stderr, "  [REORDER] sub%d: pub %u seq %u @pos %" PRIu64
                     " after prev seq %u @pos %" PRIu64
                     " (delta=%d, lost=%" PRIu64 ", recv=%" PRIu64 ")\n",
                     result.sub_id, msg.pub_id, msg.seq, ring_pos,
                     prev_seq, prev_pos,
                     delta, result.lost, result.received);

        if (result.reordered == 0)
        {
            std::fprintf(stderr, "  Recent messages (oldest first):\n");
            std::size_t start = 0;
            if (trace_pos > TRACE_SIZE)
            {
                start = trace_pos - TRACE_SIZE;
            }
            for (std::size_t i = start; i < trace_pos; ++i)
            {
                auto& t = trace[i % TRACE_SIZE];
                char const* marker = "";
                if (t.pub_id == msg.pub_id)
                {
                    marker = " <--";
                }
                std::fprintf(stderr, "    [%zu] pub %u seq %u @pos %" PRIu64 "%s\n",
                             i, t.pub_id, t.seq, t.ring_pos, marker);
            }
        }

        ++result.reordered;
        return;
    }
    prev_seq = msg.seq;
    prev_pos = ring_pos;

    ++result.received;
}

inline SubResult subscriber_thread_copy(kickmsg::SharedRegion& region, int sub_id,
                                        int num_pubs, uint32_t /*msgs_per_pub*/)
{
    kickmsg::Subscriber sub{region};

    SubResult result{};
    result.sub_id = sub_id;

    std::vector<uint32_t> last_seq(static_cast<std::size_t>(num_pubs), UINT32_MAX);
    std::vector<uint64_t> last_pos(static_cast<std::size_t>(num_pubs), UINT64_MAX);
    MsgTrace trace[TRACE_SIZE]{};
    std::size_t trace_pos = 0;

    auto const timeout = milliseconds{500};

    while (true)
    {
        auto sample = sub.receive(timeout);
        if (not sample)
        {
            if (g_all_publishers_done)
            {
                sample = sub.try_receive();
                if (not sample)
                {
                    break;
                }
            }
            else
            {
                continue;
            }
        }

        if (sample->len() != sizeof(Payload))
        {
            ++result.corrupted;
            continue;
        }

        Payload msg;
        std::memcpy(&msg, sample->data(), sizeof(msg));
        validate_payload(msg, num_pubs, sample->ring_pos(),
                         last_seq, last_pos, result, trace, trace_pos);
    }

    result.lost = sub.lost();
    return result;
}

inline SubResult subscriber_thread_zerocopy(kickmsg::SharedRegion& region, int sub_id,
                                            int num_pubs, uint32_t /*msgs_per_pub*/)
{
    kickmsg::Subscriber sub{region};

    SubResult result{};
    result.sub_id = sub_id;

    std::vector<uint32_t> last_seq(static_cast<std::size_t>(num_pubs), UINT32_MAX);
    std::vector<uint64_t> last_pos(static_cast<std::size_t>(num_pubs), UINT64_MAX);
    MsgTrace trace[TRACE_SIZE]{};
    std::size_t trace_pos = 0;

    auto const timeout = milliseconds{500};

    while (true)
    {
        auto view = sub.receive_view(timeout);
        if (not view)
        {
            if (g_all_publishers_done)
            {
                view = sub.try_receive_view();
                if (not view)
                {
                    break;
                }
            }
            else
            {
                continue;
            }
        }

        if (view->len() != sizeof(Payload))
        {
            ++result.corrupted;
            continue;
        }

        Payload msg;
        std::memcpy(&msg, view->data(), sizeof(msg));
        validate_payload(msg, num_pubs, view->ring_pos(),
                         last_seq, last_pos, result, trace, trace_pos);
    }

    result.lost = sub.lost();
    return result;
}

inline bool verify_pool_free(kickmsg::SharedRegion& region, kickmsg::channel::Config const& cfg)
{
    auto* base = region.base();
    auto* hdr  = region.header();

    std::vector<bool> seen(cfg.pool_size, false);
    uint32_t count  = 0;
    uint32_t top    = kickmsg::tagged_idx(hdr->free_top.load(std::memory_order_acquire));

    while (top != kickmsg::INVALID_SLOT)
    {
        if (top >= cfg.pool_size)
        {
            std::fprintf(stderr, "  [FAIL] free stack contains out-of-range index %u\n", top);
            return false;
        }
        if (seen[top])
        {
            std::fprintf(stderr, "  [FAIL] free stack contains duplicate slot %u\n", top);
            return false;
        }
        seen[top] = true;
        ++count;

        auto* slot = kickmsg::slot_at(base, hdr, top);
        top = slot->next_free;
    }

    if (count != cfg.pool_size)
    {
        std::fprintf(stderr, "  [FAIL] free stack has %u slots, expected %zu (leak!)\n",
                     count, cfg.pool_size);
        return false;
    }
    return true;
}

inline bool verify_rings_inactive(kickmsg::SharedRegion& region, kickmsg::channel::Config const& cfg)
{
    auto* base = region.base();
    auto* hdr  = region.header();

    for (uint32_t i = 0; i < cfg.max_subscribers; ++i)
    {
        auto* ring = kickmsg::sub_ring_at(base, hdr, i);
        uint32_t packed = ring->state_flight.load(std::memory_order_acquire);
        if (kickmsg::ring::get_state(packed) != kickmsg::ring::Free)
        {
            std::fprintf(stderr, "  [FAIL] ring %u not Free after test\n", i);
            return false;
        }
        if (kickmsg::ring::get_in_flight(packed) != 0)
        {
            std::fprintf(stderr, "  [FAIL] ring %u has in_flight=%u after test\n",
                         i, kickmsg::ring::get_in_flight(packed));
            return false;
        }
    }
    return true;
}

inline bool verify_refcounts_zero(kickmsg::SharedRegion& region, kickmsg::channel::Config const& cfg)
{
    auto* base = region.base();
    auto* hdr  = region.header();

    for (uint32_t i = 0; i < cfg.pool_size; ++i)
    {
        auto* slot = kickmsg::slot_at(base, hdr, i);
        uint32_t rc = slot->refcount;
        if (rc != 0)
        {
            std::fprintf(stderr, "  [FAIL] slot %u has refcount %u (expected 0)\n", i, rc);
            return false;
        }
    }
    return true;
}

struct TestRunner
{
    int pass = 0;
    int fail = 0;

    void run(bool result)
    {
        if (result)
        {
            ++pass;
        }
        else
        {
            ++fail;
        }
    }

    int summary()
    {
        std::printf("=== Summary: %d passed, %d failed ===\n", pass, fail);
        return fail > 0 ? 1 : 0;
    }
};

#endif
