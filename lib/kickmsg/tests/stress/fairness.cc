#include "common.h"

bool run_fairness_test()
{
    std::printf("--- Fairness test: 1 pub x 100000 msgs, 16 subs (ring=256, pool=512) ---\n");

    g_all_publishers_done = false;

    constexpr int      NUM_SUBS  = 16;
    uint32_t const     NUM_MSGS  = 100000 / TSAN_SCALE;

    kickmsg::channel::Config cfg;
    cfg.max_subscribers   = NUM_SUBS;
    cfg.sub_ring_capacity = 256;
    cfg.pool_size         = 512;
    cfg.max_payload_size  = sizeof(Payload);

    char const* shm_name = "/kickmsg_fairness_test";
    kickmsg::SharedMemory::unlink(shm_name);
    auto region = kickmsg::SharedRegion::create(
        shm_name, kickmsg::channel::PubSub, cfg, "fairness");

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

    kickcat::sleep(10ms);

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

    std::size_t repaired = region.repair_locked_entries();
    if (repaired > 0)
    {
        std::printf("  GC repaired %zu locked entries\n", repaired);
    }
    std::size_t reclaimed = region.reclaim_orphaned_slots();
    if (reclaimed > 0)
    {
        std::printf("  GC reclaimed %zu orphaned slots\n", reclaimed);
    }

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
