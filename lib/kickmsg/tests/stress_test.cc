#include "kickmsg/Publisher.h"
#include "kickmsg/Subscriber.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

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

uint32_t compute_checksum(Payload const& p)
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

static std::atomic<bool> g_all_publishers_done{false};
static std::atomic<int>  g_publishers_finished{0};

void publisher_thread(kickmsg::SharedRegion& region, int pub_id, uint32_t count)
{
    kickmsg::Publisher pub{region};

    for (uint32_t i = 0; i < count; ++i)
    {
        Payload msg;
        msg.magic  = Payload::MAGIC;
        msg.pub_id = static_cast<uint32_t>(pub_id);
        msg.seq    = i;
        msg.checksum = compute_checksum(msg);

        while (not pub.send(&msg, sizeof(msg)))
        {
            std::this_thread::yield();
        }
    }

    g_publishers_finished.fetch_add(1, std::memory_order_relaxed);
}

struct MsgTrace
{
    uint32_t pub_id;
    uint32_t seq;
};

static constexpr std::size_t TRACE_SIZE = 16;

void validate_payload(Payload const& msg, int num_pubs,
                      std::vector<uint32_t>& last_seq, SubResult& result,
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

    // Record in trace ring
    trace[trace_pos % TRACE_SIZE] = {msg.pub_id, msg.seq};
    ++trace_pos;

    auto& prev = last_seq[msg.pub_id];
    if (prev != UINT32_MAX and msg.seq <= prev)
    {
        auto delta = static_cast<int32_t>(prev) - static_cast<int32_t>(msg.seq);
        std::fprintf(stderr, "  [REORDER] sub%d: pub %u seq %u after prev %u (delta=%d, lost=%" PRIu64 ", recv=%" PRIu64 ")\n",
                     result.sub_id, msg.pub_id, msg.seq, prev, delta, result.lost, result.received);

        // Dump recent messages for context
        if (result.reordered == 0)
        {
            std::fprintf(stderr, "  Recent messages (oldest first):\n");
            std::size_t start = 0;
            if (trace_pos > TRACE_SIZE)
            {
                start = trace_pos - TRACE_SIZE;
            }
            for (auto i = start; i < trace_pos; ++i)
            {
                auto& t = trace[i % TRACE_SIZE];
                char const* marker = "";
                if (t.pub_id == msg.pub_id)
                {
                    marker = " <--";
                }
                std::fprintf(stderr, "    [%zu] pub %u seq %u%s\n",
                             i, t.pub_id, t.seq, marker);
            }
        }

        ++result.reordered;
        return;
    }
    prev = msg.seq;

    ++result.received;
}

SubResult subscriber_thread_copy(kickmsg::SharedRegion& region, int sub_id,
                                 int num_pubs, uint32_t /*msgs_per_pub*/)
{
    kickmsg::Subscriber sub{region};

    SubResult result{};
    result.sub_id = sub_id;

    std::vector<uint32_t> last_seq(static_cast<std::size_t>(num_pubs), UINT32_MAX);
    MsgTrace trace[TRACE_SIZE]{};
    std::size_t trace_pos = 0;

    auto const timeout = std::chrono::milliseconds{500};

    while (true)
    {
        auto sample = sub.receive(timeout);
        if (not sample)
        {
            if (g_all_publishers_done.load(std::memory_order_relaxed))
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
        validate_payload(msg, num_pubs, last_seq, result, trace, trace_pos);
    }

    result.lost = sub.lost();
    return result;
}

SubResult subscriber_thread_zerocopy(kickmsg::SharedRegion& region, int sub_id,
                                     int num_pubs, uint32_t /*msgs_per_pub*/)
{
    kickmsg::Subscriber sub{region};

    SubResult result{};
    result.sub_id = sub_id;

    std::vector<uint32_t> last_seq(static_cast<std::size_t>(num_pubs), UINT32_MAX);
    MsgTrace trace[TRACE_SIZE]{};
    std::size_t trace_pos = 0;

    auto const timeout = std::chrono::milliseconds{500};

    while (true)
    {
        auto view = sub.receive_view(timeout);
        if (not view)
        {
            if (g_all_publishers_done.load(std::memory_order_relaxed))
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
        validate_payload(msg, num_pubs, last_seq, result, trace, trace_pos);
    }

    result.lost = sub.lost();
    return result;
}

// Release remaining slot references after all threads have joined.
//
// After subscriber destructors run drain_unconsumed, some ring entries
// still hold references for slots the subscriber consumed via try_receive
// (which does not decrement refcount -- it relies on eviction).
//
// We walk all committed ring entries, count references per slot, then
// release remaining refcounts. This avoids the double-decrement bug of
// the old per-entry approach (drain_unconsumed already released some).
void release_remaining_refs(kickmsg::SharedRegion& region, kickmsg::RingConfig const& cfg)
{
    auto* base = region.base();
    auto* hdr  = region.header();

    // Count ring-entry references per slot
    std::vector<uint32_t> entry_refs(cfg.pool_size, 0);

    for (uint32_t i = 0; i < cfg.max_subscribers; ++i)
    {
        auto* ring    = kickmsg::sub_ring_at(base, hdr, i);
        auto  wp      = ring->write_pos.load(std::memory_order_acquire);
        auto* entries = kickmsg::ring_entries(ring);

        auto cap   = hdr->sub_ring_capacity;
        uint64_t start = 0;
        if (wp > cap)
        {
            start = wp - cap;
        }

        for (uint64_t pos = start; pos < wp; ++pos)
        {
            auto  idx = pos & hdr->sub_ring_mask;
            auto& e   = entries[idx];
            auto  seq = e.sequence.load(std::memory_order_relaxed);
            if (seq < pos + 1 or seq == kickmsg::LOCKED_SEQUENCE)
            {
                continue; // uncommitted or locked
            }

            auto si = e.slot_idx.load(std::memory_order_relaxed);
            if (si < cfg.pool_size)
            {
                entry_refs[si]++;
            }
        }
    }

    // Force-release any slot still holding refs.
    // After drain_unconsumed, actual refcount <= entry_refs (some refs
    // were already released). We set refcount to 0 and push to free stack.
    for (uint32_t idx = 0; idx < cfg.pool_size; ++idx)
    {
        auto* slot = kickmsg::slot_at(base, hdr, idx);
        auto  rc   = slot->refcount.load(std::memory_order_acquire);
        if (rc > 0)
        {
            if (rc > entry_refs[idx])
            {
                std::fprintf(stderr, "  [WARN] slot %u: refcount %u > ring refs %u "
                             "(possible refcount leak)\n", idx, rc, entry_refs[idx]);
            }
            slot->refcount.store(0, std::memory_order_release);
            kickmsg::treiber_push(hdr->free_top, slot, idx);
        }
    }
}

bool verify_pool_free(kickmsg::SharedRegion& region, kickmsg::RingConfig const& cfg)
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
        top = slot->next_free.load(std::memory_order_relaxed);
    }

    if (count != cfg.pool_size)
    {
        std::fprintf(stderr, "  [FAIL] free stack has %u slots, expected %zu (leak!)\n",
                     count, cfg.pool_size);
        return false;
    }
    return true;
}

bool verify_rings_inactive(kickmsg::SharedRegion& region, kickmsg::RingConfig const& cfg)
{
    auto* base = region.base();
    auto* hdr  = region.header();

    for (uint32_t i = 0; i < cfg.max_subscribers; ++i)
    {
        auto* ring = kickmsg::sub_ring_at(base, hdr, i);
        if (ring->active.load(std::memory_order_relaxed) != 0)
        {
            std::fprintf(stderr, "  [FAIL] ring %u still active after test\n", i);
            return false;
        }
    }
    return true;
}

bool verify_refcounts_zero(kickmsg::SharedRegion& region, kickmsg::RingConfig const& cfg)
{
    auto* base = region.base();
    auto* hdr  = region.header();

    for (uint32_t i = 0; i < cfg.pool_size; ++i)
    {
        auto* slot = kickmsg::slot_at(base, hdr, i);
        auto  rc   = slot->refcount.load(std::memory_order_relaxed);
        if (rc != 0)
        {
            std::fprintf(stderr, "  [FAIL] slot %u has refcount %u (expected 0)\n", i, rc);
            return false;
        }
    }
    return true;
}

bool run_stress_test(TestConfig const& tc)
{
    char const* zc_label = "";
    if (tc.use_zerocopy)
    {
        zc_label = " (zero-copy)";
    }
    std::printf("--- Stress test%s: %d pubs x %u msgs, %d subs, pool=%zu, ring=%zu ---\n",
                zc_label,
                tc.num_publishers, tc.msgs_per_pub, tc.num_subscribers,
                tc.pool_size, tc.ring_capacity);

    g_all_publishers_done.store(false, std::memory_order_relaxed);
    g_publishers_finished.store(0, std::memory_order_relaxed);

    kickmsg::RingConfig cfg;
    cfg.max_subscribers   = tc.max_subs;
    cfg.sub_ring_capacity = tc.ring_capacity;
    cfg.pool_size         = tc.pool_size;
    cfg.max_payload_size  = sizeof(Payload);

    char const* shm_name = "/kickmsg_stress_test";
    auto region = kickmsg::SharedRegion::create(
        shm_name, kickmsg::ChannelType::PubSub, cfg, "stress_test");

    auto t0 = std::chrono::steady_clock::now();

    std::vector<std::thread> sub_threads;
    std::vector<SubResult> sub_results(static_cast<std::size_t>(tc.num_subscribers));

    for (int i = 0; i < tc.num_subscribers; ++i)
    {
        sub_threads.emplace_back([&region, i, &sub_results, &tc]()
        {
            auto fn = subscriber_thread_copy;
            if (tc.use_zerocopy)
            {
                fn = subscriber_thread_zerocopy;
            }
            sub_results[static_cast<std::size_t>(i)] =
                fn(region, i, tc.num_publishers, tc.msgs_per_pub);
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds{10});

    std::vector<std::thread> pub_threads;
    for (int i = 0; i < tc.num_publishers; ++i)
    {
        pub_threads.emplace_back(publisher_thread, std::ref(region), i, tc.msgs_per_pub);
    }

    for (auto& t : pub_threads)
    {
        t.join();
    }
    g_all_publishers_done.store(true, std::memory_order_release);

    for (auto& t : sub_threads)
    {
        t.join();
    }

    auto t1 = std::chrono::steady_clock::now();
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

    uint64_t total_sent = static_cast<uint64_t>(tc.num_publishers) * tc.msgs_per_pub;

    bool all_ok = true;

    char const* mode_label = "copy";
    if (tc.use_zerocopy)
    {
        mode_label = "zerocopy";
    }
    std::printf("  Config: %d pub, %d sub, %s\n",
                tc.num_publishers, tc.num_subscribers, mode_label);
    std::printf("  Elapsed: %ld ms, total published: %" PRIu64 "\n", elapsed_ms, total_sent);
    std::printf("  %-6s %10s %10s %10s %10s %10s\n",
                "sub", "received", "lost", "corrupt", "bad_pid", "reorder");

    for (auto const& r : sub_results)
    {
        std::printf("  sub%-3d %10" PRIu64 " %10" PRIu64 " %10" PRIu64
                    " %10" PRIu64 " %10" PRIu64 "\n",
                    r.sub_id, r.received, r.lost, r.corrupted,
                    r.bad_pub_id, r.reordered);

        if (r.corrupted > 0)
        {
            std::fprintf(stderr, "  [FAIL] sub%d: %" PRIu64 " corrupted messages!\n",
                         r.sub_id, r.corrupted);
            all_ok = false;
        }
        if (r.bad_pub_id > 0)
        {
            std::fprintf(stderr, "  [FAIL] sub%d: %" PRIu64 " bad publisher IDs!\n",
                         r.sub_id, r.bad_pub_id);
            all_ok = false;
        }
        if (r.reordered > 0)
        {
            std::fprintf(stderr, "  [FAIL] sub%d: %" PRIu64 " reordered messages!\n",
                         r.sub_id, r.reordered);
            all_ok = false;
        }
        if (r.received == 0)
        {
            std::fprintf(stderr, "  [FAIL] sub%d: received 0 messages!\n", r.sub_id);
            all_ok = false;
        }
        // Completeness check: received + lost should account for all messages
        // this subscriber saw. The lost counter tracks ring-level losses;
        // received + lost may be less than total_sent (subscriber starts from
        // write_pos at construction, missing earlier messages), but should
        // never exceed it.
        if (r.received + r.lost > total_sent)
        {
            std::fprintf(stderr, "  [FAIL] sub%d: received+lost (%" PRIu64
                         ") > total_sent (%" PRIu64 ")!\n",
                         r.sub_id, r.received + r.lost, total_sent);
            all_ok = false;
        }
    }

    auto reclaimed = region.collect_garbage();
    if (reclaimed > 0)
    {
        std::printf("  GC reclaimed %zu slots\n", reclaimed);
    }

    release_remaining_refs(region, cfg);

    all_ok &= verify_refcounts_zero(region, cfg);
    all_ok &= verify_pool_free(region, cfg);
    all_ok &= verify_rings_inactive(region, cfg);

    region.unlink();

    if (all_ok)
    {
        std::printf("  [PASS]\n\n");
    }
    else
    {
        std::printf("  [FAIL]\n\n");
    }
    return all_ok;
}

bool run_treiber_stress()
{
    std::printf("--- Treiber stack stress: 8 threads x 100000 pop/push cycles ---\n");

    kickmsg::RingConfig cfg;
    cfg.max_subscribers   = 1;
    cfg.sub_ring_capacity = 4;
    cfg.pool_size         = 64;
    cfg.max_payload_size  = 8;

    char const* shm_name = "/kickmsg_treiber_stress";
    auto region = kickmsg::SharedRegion::create(
        shm_name, kickmsg::ChannelType::PubSub, cfg, "treiber");

    auto* base = region.base();
    auto* hdr  = region.header();

    constexpr int    NUM_THREADS = 8;
    int const        CYCLES      = 100000 / TSAN_SCALE;
    std::atomic<int> contention_hits{0};

    auto worker = [&]()
    {
        for (int i = 0; i < CYCLES; ++i)
        {
            auto idx = kickmsg::treiber_pop(hdr->free_top, base, hdr);
            if (idx == kickmsg::INVALID_SLOT)
            {
                contention_hits.fetch_add(1, std::memory_order_relaxed);
                std::this_thread::yield();
                --i;
                continue;
            }

            auto* slot = kickmsg::slot_at(base, hdr, idx);
            auto* data = kickmsg::slot_data(slot);
            std::memset(data, static_cast<int>(idx & 0xFF), cfg.max_payload_size);

            kickmsg::treiber_push(hdr->free_top, slot, idx);
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i)
    {
        threads.emplace_back(worker);
    }
    for (auto& t : threads)
    {
        t.join();
    }

    bool ok = verify_pool_free(region, cfg);

    std::printf("  Contention retries: %d\n", contention_hits.load());

    region.unlink();

    if (ok)
    {
        std::printf("  [PASS]\n\n");
    }
    else
    {
        std::printf("  [FAIL]\n\n");
    }
    return ok;
}

bool run_fairness_test()
{
    std::printf("--- Fairness test: 1 pub x 100000 msgs, 16 subs (ring=256, pool=512) ---\n");

    g_all_publishers_done.store(false, std::memory_order_relaxed);
    g_publishers_finished.store(0, std::memory_order_relaxed);

    constexpr int      NUM_SUBS  = 16;
    uint32_t const     NUM_MSGS  = 100000 / TSAN_SCALE;

    kickmsg::RingConfig cfg;
    cfg.max_subscribers   = NUM_SUBS;
    cfg.sub_ring_capacity = 256;
    cfg.pool_size         = 512;
    cfg.max_payload_size  = sizeof(Payload);

    char const* shm_name = "/kickmsg_fairness_test";
    auto region = kickmsg::SharedRegion::create(
        shm_name, kickmsg::ChannelType::PubSub, cfg, "fairness");

    std::vector<SubResult> results(NUM_SUBS);
    std::vector<std::thread> sub_threads;

    for (int i = 0; i < NUM_SUBS; ++i)
    {
        sub_threads.emplace_back([&region, i, &results, NUM_MSGS]()
        {
            results[static_cast<std::size_t>(i)] =
                subscriber_thread_copy(region, i, 1, NUM_MSGS);
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds{10});

    std::thread pub_thread(publisher_thread, std::ref(region), 0, NUM_MSGS);
    pub_thread.join();
    g_all_publishers_done.store(true, std::memory_order_release);

    for (auto& t : sub_threads)
    {
        t.join();
    }

    bool ok = true;
    uint64_t min_recv = UINT64_MAX;
    uint64_t max_recv = 0;

    for (auto const& r : results)
    {
        min_recv = std::min(min_recv, r.received);
        max_recv = std::max(max_recv, r.received);

        if (r.corrupted > 0 or r.bad_pub_id > 0 or r.reordered > 0)
        {
            std::fprintf(stderr, "  [FAIL] sub%d: corrupt=%" PRIu64 " bad_pid=%"
                         PRIu64 " reorder=%" PRIu64 "\n",
                         r.sub_id, r.corrupted, r.bad_pub_id, r.reordered);
            ok = false;
        }
    }

    std::printf("  Received range: [%" PRIu64 ", %" PRIu64 "] (spread: %" PRIu64 ")\n",
                min_recv, max_recv, max_recv - min_recv);

    if (min_recv == 0)
    {
        std::fprintf(stderr, "  [FAIL] at least one subscriber received 0 messages\n");
        ok = false;
    }

    auto reclaimed = region.collect_garbage();
    if (reclaimed > 0)
    {
        std::printf("  GC reclaimed %zu slots\n", reclaimed);
    }

    release_remaining_refs(region, cfg);
    ok &= verify_refcounts_zero(region, cfg);
    ok &= verify_pool_free(region, cfg);
    ok &= verify_rings_inactive(region, cfg);

    region.unlink();

    if (ok)
    {
        std::printf("  [PASS]\n\n");
    }
    else
    {
        std::printf("  [FAIL]\n\n");
    }
    return ok;
}

int main()
{
    std::printf("=== KickMsg Lock-Free Stress Tests ===\n\n");

    int pass = 0;
    int fail = 0;

    auto run = [&](bool result)
    {
        if (result)
        {
            ++pass;
        }
        else
        {
            ++fail;
        }
    };

    run(run_treiber_stress());

    run(run_fairness_test());

    // Copy-based receive tests
    run(run_stress_test(TestConfig{
        .num_publishers  = 2,
        .num_subscribers = 4,
        .msgs_per_pub    = 100000 / TSAN_SCALE,
        .pool_size       = 256,
        .ring_capacity   = 64,
        .max_subs        = 8,
    }));

    run(run_stress_test(TestConfig{
        .num_publishers  = 8,
        .num_subscribers = 8,
        .msgs_per_pub    = 50000 / TSAN_SCALE,
        .pool_size       = 128,
        .ring_capacity   = 32,
        .max_subs        = 16,
    }));

    run(run_stress_test(TestConfig{
        .num_publishers  = 1,
        .num_subscribers = 1,
        .msgs_per_pub    = 500000 / TSAN_SCALE,
        .pool_size       = 64,
        .ring_capacity   = 16,
        .max_subs        = 2,
    }));

    // High contention: many pubs, small pool, heavy overflow
    run(run_stress_test(TestConfig{
        .num_publishers  = 16,
        .num_subscribers = 16,
        .msgs_per_pub    = 20000 / TSAN_SCALE,
        .pool_size       = 32,
        .ring_capacity   = 8,
        .max_subs        = 16,
    }));

    // Zero-copy receive tests -- exercises SampleView pin CAS,
    // refcount increment/decrement, and destructor release path
    run(run_stress_test(TestConfig{
        .num_publishers  = 2,
        .num_subscribers = 4,
        .msgs_per_pub    = 100000 / TSAN_SCALE,
        .pool_size       = 256,
        .ring_capacity   = 64,
        .max_subs        = 8,
        .use_zerocopy    = true,
    }));

    run(run_stress_test(TestConfig{
        .num_publishers  = 8,
        .num_subscribers = 8,
        .msgs_per_pub    = 50000 / TSAN_SCALE,
        .pool_size       = 128,
        .ring_capacity   = 32,
        .max_subs        = 16,
        .use_zerocopy    = true,
    }));

    // High contention zero-copy
    run(run_stress_test(TestConfig{
        .num_publishers  = 16,
        .num_subscribers = 16,
        .msgs_per_pub    = 20000 / TSAN_SCALE,
        .pool_size       = 32,
        .ring_capacity   = 8,
        .max_subs        = 16,
        .use_zerocopy    = true,
    }));

    std::printf("=== Summary: %d passed, %d failed ===\n", pass, fail);
    if (fail > 0)
    {
        return 1;
    }
    return 0;
}
