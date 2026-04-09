#include "common.h"

bool run_subscriber_churn()
{
    std::printf("--- Subscriber churn: subs join/leave while publisher is active ---\n");

    char const* shm_name = "/kickmsg_churn_test";
    kickmsg::SharedMemory::unlink(shm_name);

    kickmsg::channel::Config cfg;
    cfg.max_subscribers   = 4;
    cfg.sub_ring_capacity = 32;
    cfg.pool_size         = 128;
    cfg.max_payload_size  = sizeof(Payload);

    auto region = kickmsg::SharedRegion::create(
        shm_name, kickmsg::channel::PubSub, cfg, "churn");

    constexpr uint32_t NUM_MSGS = 10000 / TSAN_SCALE;
    std::atomic<bool> pub_done{false};

    // Publisher runs continuously
    std::thread pub_thread([&]()
    {
        kickmsg::Publisher pub(region);
        for (uint32_t i = 0; i < NUM_MSGS; ++i)
        {
            Payload msg;
            msg.magic    = Payload::MAGIC;
            msg.pub_id   = 0;
            msg.seq      = i;
            msg.checksum = compute_checksum(msg);

            while (pub.send(&msg, sizeof(msg)) < 0)
            {
                kickcat::sleep(0ns);
            }
        }
        pub_done = true;
    });

    // Subscriber threads join, consume a few messages, then leave -- repeatedly
    constexpr int CHURN_ROUNDS = 5;
    std::atomic<bool> error{false};

    auto churner = [&]()
    {
        for (int round = 0; round < CHURN_ROUNDS and not pub_done and not error; ++round)
        {
            kickmsg::Subscriber sub(region);

            // Consume a few messages
            for (int j = 0; j < 10; ++j)
            {
                auto sample = sub.try_receive();
                if (sample and sample->len() == sizeof(Payload))
                {
                    Payload msg;
                    std::memcpy(&msg, sample->data(), sizeof(msg));
                    if (msg.magic != Payload::MAGIC or msg.checksum != compute_checksum(msg))
                    {
                        error = true;
                    }
                }
            }
            // Subscriber destructor runs here: active=0, in_flight spin, drain
        }
    };

    std::vector<std::thread> churners;
    for (int i = 0; i < 4; ++i)
    {
        churners.emplace_back(churner);
    }

    pub_thread.join();
    for (auto& t : churners)
    {
        t.join();
    }

    bool ok = not error;

    // After all threads joined, every slot should be back in the pool
    std::size_t repaired = region.repair_locked_entries();
    std::size_t reclaimed = region.reclaim_orphaned_slots();
    if (repaired > 0)
    {
        std::printf("  GC repaired %zu locked entries\n", repaired);
    }
    if (reclaimed > 0)
    {
        std::printf("  GC reclaimed %zu orphaned slots\n", reclaimed);
    }

    ok &= verify_refcounts_zero(region, cfg);
    ok &= verify_pool_free(region, cfg);

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
