#include "common.h"

bool run_single_slot_ring()
{
    std::printf("--- Edge case: single-slot ring (ring=2, pool=32, 4 pubs) ---\n");

    g_all_publishers_done = false;

    constexpr int  NUM_PUBS = 4;
    constexpr int  NUM_SUBS = 4;
    uint32_t const NUM_MSGS = 10000 / TSAN_SCALE;

    kickmsg::channel::Config cfg;
    cfg.max_subscribers   = 4;
    cfg.sub_ring_capacity = 2;  // Smallest valid power of 2
    cfg.pool_size         = 32;
    cfg.max_payload_size  = sizeof(Payload);

    char const* shm_name = "/kickmsg_single_slot_ring";
    kickmsg::SharedMemory::unlink(shm_name);
    auto region = kickmsg::SharedRegion::create(
        shm_name, kickmsg::channel::PubSub, cfg, "single_slot_ring");

    nanoseconds t0 = kickcat::since_epoch();

    std::vector<SubResult> sub_results(NUM_SUBS);
    std::vector<std::thread> sub_threads;

    for (int i = 0; i < NUM_SUBS; ++i)
    {
        sub_threads.emplace_back([&region, i, &sub_results, NUM_MSGS]()
        {
            sub_results[static_cast<std::size_t>(i)] =
                subscriber_thread_copy(region, i, NUM_PUBS, NUM_MSGS);
        });
    }

    kickcat::sleep(10ms);

    std::vector<std::thread> pub_threads;
    for (int i = 0; i < NUM_PUBS; ++i)
    {
        pub_threads.emplace_back(publisher_thread, std::ref(region), i, NUM_MSGS);
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

    uint64_t total_sent = static_cast<uint64_t>(NUM_PUBS) * NUM_MSGS;

    bool ok = true;

    std::printf("  Elapsed: %ld ms, total published: %" PRIu64 "\n", elapsed_ms, total_sent);

    for (auto const& r : sub_results)
    {
        if (r.corrupted > 0)
        {
            std::fprintf(stderr, "  [FAIL] sub%d: %" PRIu64 " corrupted messages!\n",
                         r.sub_id, r.corrupted);
            ok = false;
        }
        if (r.bad_pub_id > 0)
        {
            std::fprintf(stderr, "  [FAIL] sub%d: %" PRIu64 " bad publisher IDs!\n",
                         r.sub_id, r.bad_pub_id);
            ok = false;
        }
        if (r.reordered > 0)
        {
            std::fprintf(stderr, "  [FAIL] sub%d: %" PRIu64 " reordered messages!\n",
                         r.sub_id, r.reordered);
            ok = false;
        }
        if (r.received == 0)
        {
            std::fprintf(stderr, "  [FAIL] sub%d: received 0 messages!\n", r.sub_id);
            ok = false;
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

bool run_subscriber_saturation()
{
    std::printf("--- Edge case: subscriber saturation (max_subs=4, create 5th) ---\n");

    g_all_publishers_done = false;

    kickmsg::channel::Config cfg;
    cfg.max_subscribers   = 4;
    cfg.sub_ring_capacity = 8;
    cfg.pool_size         = 16;
    cfg.max_payload_size  = sizeof(Payload);

    char const* shm_name = "/kickmsg_sub_saturation";
    kickmsg::SharedMemory::unlink(shm_name);
    auto region = kickmsg::SharedRegion::create(
        shm_name, kickmsg::channel::PubSub, cfg, "sub_saturation");

    bool ok = true;

    // Create 4 subscribers (should succeed)
    std::vector<std::unique_ptr<kickmsg::Subscriber>> subs;
    for (int i = 0; i < 4; ++i)
    {
        try
        {
            subs.push_back(std::make_unique<kickmsg::Subscriber>(region));
        }
        catch (std::exception const& e)
        {
            std::fprintf(stderr, "  [FAIL] subscriber %d creation failed: %s\n", i, e.what());
            ok = false;
        }
    }

    // Try 5th subscriber (should throw)
    bool threw = false;
    try
    {
        kickmsg::Subscriber extra(region);
    }
    catch (std::exception const&)
    {
        threw = true;
    }

    if (not threw)
    {
        std::fprintf(stderr, "  [FAIL] 5th subscriber did not throw\n");
        ok = false;
    }
    else
    {
        std::printf("  5th subscriber correctly rejected\n");
    }

    // Destroy one subscriber
    subs.pop_back();

    // Create replacement (should succeed)
    try
    {
        subs.push_back(std::make_unique<kickmsg::Subscriber>(region));
        std::printf("  Replacement subscriber created successfully\n");
    }
    catch (std::exception const& e)
    {
        std::fprintf(stderr, "  [FAIL] replacement subscriber failed: %s\n", e.what());
        ok = false;
    }

    // Publisher sends some messages while subscribers are active
    {
        kickmsg::Publisher pub{region};
        uint32_t const NUM_MSGS = 1000 / TSAN_SCALE;

        for (uint32_t i = 0; i < NUM_MSGS; ++i)
        {
            Payload msg;
            msg.magic    = Payload::MAGIC;
            msg.pub_id   = 0;
            msg.seq      = i;
            msg.checksum = compute_checksum(msg);

            int32_t rc;
            while ((rc = pub.send(&msg, sizeof(msg))) < 0)
            {
                if (rc != -EAGAIN)
                {
                    std::fprintf(stderr, "  [FATAL] send() returned %d\n", rc);
                    std::abort();
                }
                kickcat::sleep(0ns);
            }
        }
    }

    // Consume messages from each subscriber to verify no corruption
    for (std::size_t i = 0; i < subs.size(); ++i)
    {
        uint64_t received  = 0;
        uint64_t corrupted = 0;

        while (true)
        {
            auto sample = subs[i]->try_receive();
            if (not sample)
            {
                break;
            }
            if (sample->len() == sizeof(Payload))
            {
                Payload msg;
                std::memcpy(&msg, sample->data(), sizeof(msg));
                if (msg.magic != Payload::MAGIC or msg.checksum != compute_checksum(msg))
                {
                    ++corrupted;
                }
                else
                {
                    ++received;
                }
            }
        }

        if (corrupted > 0)
        {
            std::fprintf(stderr, "  [FAIL] sub %zu: %" PRIu64 " corrupted messages!\n",
                         i, corrupted);
            ok = false;
        }
        std::printf("  sub %zu: received %" PRIu64 " messages\n", i, received);
    }

    // Destroy all subscribers before verification
    subs.clear();

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
