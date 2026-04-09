#include "common.h"

bool run_treiber_stress()
{
    std::printf("--- Treiber stack stress: 8 threads x 100000 pop/push cycles ---\n");

    kickmsg::channel::Config cfg;
    cfg.max_subscribers   = 1;
    cfg.sub_ring_capacity = 4;
    cfg.pool_size         = 64;
    cfg.max_payload_size  = 8;

    char const* shm_name = "/kickmsg_treiber_stress";
    kickmsg::SharedMemory::unlink(shm_name);
    auto region = kickmsg::SharedRegion::create(
        shm_name, kickmsg::channel::PubSub, cfg, "treiber");

    auto* base = region.base();
    auto* hdr  = region.header();

    constexpr int    NUM_THREADS = 8;
    int const        CYCLES      = 100000 / TSAN_SCALE;
    std::atomic<int> contention_hits{0};

    auto worker = [&]()
    {
        for (int i = 0; i < CYCLES; ++i)
        {
            uint32_t idx = kickmsg::treiber_pop(hdr->free_top, base, hdr);
            if (idx == kickmsg::INVALID_SLOT)
            {
                contention_hits.fetch_add(1);
                kickcat::sleep(0ns);
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
