#include "common.h"

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

    g_all_publishers_done = false;

    kickmsg::channel::Config cfg;
    cfg.max_subscribers   = tc.max_subs;
    cfg.sub_ring_capacity = tc.ring_capacity;
    cfg.pool_size         = tc.pool_size;
    cfg.max_payload_size  = sizeof(Payload);

    char const* shm_name = "/kickmsg_stress_test";
    kickmsg::SharedMemory::unlink(shm_name);
    auto region = kickmsg::SharedRegion::create(
        shm_name, kickmsg::channel::PubSub, cfg, "stress_test");

    nanoseconds t0 = kickcat::since_epoch();

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

    kickcat::sleep(10ms);

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

    nanoseconds t1 = kickcat::since_epoch();
    int64_t elapsed_ms = std::chrono::duration_cast<milliseconds>(t1 - t0).count();

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

void run_all_mpmc(TestRunner& runner)
{
    // Copy-based receive tests
    {
        TestConfig tc;
        tc.num_publishers  = 2;
        tc.num_subscribers = 4;
        tc.msgs_per_pub    = 100000 / TSAN_SCALE;
        tc.pool_size       = 256;
        tc.ring_capacity   = 64;
        tc.max_subs        = 8;
        runner.run(run_stress_test(tc));
    }

    {
        TestConfig tc;
        tc.num_publishers  = 8;
        tc.num_subscribers = 8;
        tc.msgs_per_pub    = 50000 / TSAN_SCALE;
        tc.pool_size       = 128;
        tc.ring_capacity   = 32;
        tc.max_subs        = 16;
        runner.run(run_stress_test(tc));
    }

    {
        TestConfig tc;
        tc.num_publishers  = 1;
        tc.num_subscribers = 1;
        tc.msgs_per_pub    = 500000 / TSAN_SCALE;
        tc.pool_size       = 64;
        tc.ring_capacity   = 16;
        tc.max_subs        = 2;
        runner.run(run_stress_test(tc));
    }

    // High contention: many pubs, small pool, heavy overflow
    {
        TestConfig tc;
        tc.num_publishers  = 16;
        tc.num_subscribers = 16;
        tc.msgs_per_pub    = 20000 / TSAN_SCALE;
        tc.pool_size       = 32;
        tc.ring_capacity   = 8;
        tc.max_subs        = 16;
        runner.run(run_stress_test(tc));
    }

    // Zero-copy receive tests -- exercises SampleView pin CAS,
    // refcount increment/decrement, and destructor release path
    {
        TestConfig tc;
        tc.num_publishers  = 2;
        tc.num_subscribers = 4;
        tc.msgs_per_pub    = 100000 / TSAN_SCALE;
        tc.pool_size       = 256;
        tc.ring_capacity   = 64;
        tc.max_subs        = 8;
        tc.use_zerocopy    = true;
        runner.run(run_stress_test(tc));
    }

    {
        TestConfig tc;
        tc.num_publishers  = 8;
        tc.num_subscribers = 8;
        tc.msgs_per_pub    = 50000 / TSAN_SCALE;
        tc.pool_size       = 128;
        tc.ring_capacity   = 32;
        tc.max_subs        = 16;
        tc.use_zerocopy    = true;
        runner.run(run_stress_test(tc));
    }

    // High contention zero-copy
    {
        TestConfig tc;
        tc.num_publishers  = 16;
        tc.num_subscribers = 16;
        tc.msgs_per_pub    = 20000 / TSAN_SCALE;
        tc.pool_size       = 32;
        tc.ring_capacity   = 8;
        tc.max_subs        = 16;
        tc.use_zerocopy    = true;
        runner.run(run_stress_test(tc));
    }
}
