#include "common.h"

bool run_live_repair()
{
    std::printf("--- Live repair: injector poisons entries, healer repairs under traffic ---\n");

    g_all_publishers_done = false;

    kickmsg::channel::Config cfg;
    cfg.max_subscribers   = 8;
    cfg.sub_ring_capacity = 32;
    cfg.pool_size         = 128;
    cfg.max_payload_size  = sizeof(Payload);

    char const* shm_name = "/kickmsg_live_repair";
    kickmsg::SharedMemory::unlink(shm_name);
    auto region = kickmsg::SharedRegion::create(
        shm_name, kickmsg::channel::PubSub, cfg, "live_repair");

    auto* base = region.base();
    auto* hdr  = region.header();

    constexpr int NUM_PUBS = 4;
    constexpr int NUM_SUBS = 4;

    auto const test_duration = (TSAN_SCALE > 1) ? milliseconds{200} : seconds{2};

    std::atomic<bool> stop{false};
    std::atomic<uint64_t> inject_count{0};
    std::atomic<uint64_t> heal_count{0};

    // Publisher threads: send continuously until stopped
    auto pub_worker = [&](int pub_id)
    {
        kickmsg::Publisher pub{region};
        uint32_t seq = 0;

        while (not stop)
        {
            Payload msg;
            msg.magic    = Payload::MAGIC;
            msg.pub_id   = static_cast<uint32_t>(pub_id);
            msg.seq      = seq;
            msg.checksum = compute_checksum(msg);

            int32_t rc = pub.send(&msg, sizeof(msg));
            if (rc >= 0)
            {
                ++seq;
            }
            else if (rc == -EAGAIN)
            {
                kickcat::sleep(0ns);
            }
        }
    };

    // Subscriber threads: receive continuously until done
    std::vector<SubResult> sub_results(NUM_SUBS);
    auto sub_worker = [&](int sub_id)
    {
        kickmsg::Subscriber sub{region};
        SubResult result{};
        result.sub_id = sub_id;

        std::vector<uint32_t> last_seq(NUM_PUBS, UINT32_MAX);
        std::vector<uint64_t> last_pos(NUM_PUBS, UINT64_MAX);
        MsgTrace trace[TRACE_SIZE]{};
        std::size_t trace_pos = 0;

        auto const timeout = milliseconds{100};

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
            validate_payload(msg, NUM_PUBS, sample->ring_pos(),
                             last_seq, last_pos, result, trace, trace_pos);
        }

        result.lost = sub.lost();
        sub_results[static_cast<std::size_t>(sub_id)] = result;
    };

    // Injector thread: every 10ms, poison ring 0's next entry with LOCKED_SEQUENCE
    auto injector = [&]()
    {
        while (not stop)
        {
            kickcat::sleep(10ms);

            auto* ring    = kickmsg::sub_ring_at(base, hdr, 0);
            auto* entries = kickmsg::ring_entries(ring);
            uint64_t wp   = ring->write_pos.load(std::memory_order_acquire);

            // Advance write_pos by 1 and poison the new entry
            ring->write_pos.store(wp + 1, std::memory_order_release);
            entries[(wp) & hdr->sub_ring_mask].sequence.store(
                kickmsg::LOCKED_SEQUENCE, std::memory_order_release);

            inject_count.fetch_add(1, std::memory_order_relaxed);
        }
    };

    // Healer thread: every 20ms, diagnose and repair if needed
    auto healer = [&]()
    {
        while (not stop)
        {
            kickcat::sleep(20ms);

            auto report = region.diagnose();
            if (report.locked_entries > 0)
            {
                std::size_t fixed = region.repair_locked_entries();
                heal_count.fetch_add(fixed, std::memory_order_relaxed);
            }
        }
    };

    // Start subscribers first
    std::vector<std::thread> sub_threads;
    for (int i = 0; i < NUM_SUBS; ++i)
    {
        sub_threads.emplace_back(sub_worker, i);
    }

    kickcat::sleep(10ms);

    // Start publishers
    std::vector<std::thread> pub_threads;
    for (int i = 0; i < NUM_PUBS; ++i)
    {
        pub_threads.emplace_back(pub_worker, i);
    }

    // Start injector and healer
    std::thread injector_thread(injector);
    std::thread healer_thread(healer);

    // Let it run
    kickcat::sleep(test_duration);

    // Stop everything
    stop = true;

    // Wait for publishers to finish
    for (auto& t : pub_threads)
    {
        t.join();
    }
    g_all_publishers_done.store(true, std::memory_order_release);

    injector_thread.join();
    healer_thread.join();

    for (auto& t : sub_threads)
    {
        t.join();
    }

    std::printf("  Injections: %" PRIu64 ", heals: %" PRIu64 "\n",
                inject_count.load(), heal_count.load());

    bool ok = true;

    // Check for corruption and reorders in subscriber results
    for (auto const& r : sub_results)
    {
        if (r.corrupted > 0)
        {
            std::fprintf(stderr, "  [FAIL] sub%d: %" PRIu64 " corrupted messages!\n",
                         r.sub_id, r.corrupted);
            ok = false;
        }
        if (r.reordered > 0)
        {
            std::fprintf(stderr, "  [FAIL] sub%d: %" PRIu64 " reordered messages!\n",
                         r.sub_id, r.reordered);
            ok = false;
        }
    }

    // Final GC pass
    std::size_t repaired = region.repair_locked_entries();
    if (repaired > 0)
    {
        std::printf("  GC final repaired %zu locked entries\n", repaired);
    }
    std::size_t reclaimed = region.reclaim_orphaned_slots();
    if (reclaimed > 0)
    {
        std::printf("  GC reclaimed %zu orphaned slots\n", reclaimed);
    }

    ok &= verify_pool_free(region, cfg);
    ok &= verify_refcounts_zero(region, cfg);

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
