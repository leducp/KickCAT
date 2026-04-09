#include "common.h"

bool run_pool_exhaustion()
{
    std::printf("--- Pool exhaustion: 8 pubs, pool=8, 4 slow subs ---\n");

    g_all_publishers_done = false;

    kickmsg::channel::Config cfg;
    cfg.max_subscribers   = 4;
    cfg.sub_ring_capacity = 4;
    cfg.pool_size         = 8;
    cfg.max_payload_size  = sizeof(Payload);

    char const* shm_name = "/kickmsg_pool_exhaustion";
    kickmsg::SharedMemory::unlink(shm_name);
    auto region = kickmsg::SharedRegion::create(
        shm_name, kickmsg::channel::PubSub, cfg, "pool_exhaustion");

    constexpr int  NUM_PUBS = 8;
    constexpr int  NUM_SUBS = 4;
    uint32_t const NUM_MSGS = 10000 / TSAN_SCALE;

    std::atomic<uint64_t> eagain_count{0};

    // Publishers that track EAGAIN
    auto pub_worker = [&](int pub_id)
    {
        kickmsg::Publisher pub{region};

        for (uint32_t i = 0; i < NUM_MSGS; ++i)
        {
            Payload msg;
            msg.magic    = Payload::MAGIC;
            msg.pub_id   = static_cast<uint32_t>(pub_id);
            msg.seq      = i;
            msg.checksum = compute_checksum(msg);

            int32_t rc;
            while ((rc = pub.send(&msg, sizeof(msg))) < 0)
            {
                if (rc != -EAGAIN)
                {
                    std::fprintf(stderr, "  [FATAL] publisher %d: send() returned %d\n",
                                 pub_id, rc);
                    std::abort();
                }
                eagain_count.fetch_add(1, std::memory_order_relaxed);
                kickcat::sleep(0ns);
            }
        }
    };

    // Slow subscribers: 1us sleep between receives
    auto slow_sub = [&](int sub_id)
    {
        kickmsg::Subscriber sub{region};

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

            // Deliberately slow consumer
            kickcat::sleep(1us);

            if (sample->len() == sizeof(Payload))
            {
                Payload msg;
                std::memcpy(&msg, sample->data(), sizeof(msg));
                if (msg.magic != Payload::MAGIC or msg.checksum != compute_checksum(msg))
                {
                    std::fprintf(stderr, "  [FAIL] sub%d: corruption detected\n", sub_id);
                }
            }
        }
    };

    std::vector<std::thread> sub_threads;
    for (int i = 0; i < NUM_SUBS; ++i)
    {
        sub_threads.emplace_back(slow_sub, i);
    }

    kickcat::sleep(10ms);

    std::vector<std::thread> pub_threads;
    for (int i = 0; i < NUM_PUBS; ++i)
    {
        pub_threads.emplace_back(pub_worker, i);
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

    std::printf("  EAGAIN count: %" PRIu64 "\n", eagain_count.load());

    bool ok = true;

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

    ok &= verify_pool_free(region, cfg);
    ok &= verify_refcounts_zero(region, cfg);
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
