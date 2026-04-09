#include "common.h"

bool run_gc_recovery()
{
    std::printf("--- GC recovery: repair locked entries + reclaim orphaned slots ---\n");

    char const* shm_name = "/kickmsg_gc_test";
    kickmsg::SharedMemory::unlink(shm_name);

    kickmsg::channel::Config cfg;
    cfg.max_subscribers   = 2;
    cfg.sub_ring_capacity = 8;
    cfg.pool_size         = 16;
    cfg.max_payload_size  = 64;

    auto region = kickmsg::SharedRegion::create(
        shm_name, kickmsg::channel::PubSub, cfg, "gc_test");

    auto* base = region.base();
    auto* h    = region.header();

    bool ok = true;

    // Simulate a publisher crash mid-commit: poison one entry with LOCKED_SEQUENCE.
    // To have a valid ring entry, we need an active subscriber and a committed entry first.
    {
        kickmsg::Subscriber sub(region);
        kickmsg::Publisher  pub(region);
        uint32_t val = 42;
        pub.send(&val, sizeof(val));
        // sub and pub destructor run: drain releases everything
    }

    // Now poison the committed entry
    {
        auto* ring    = kickmsg::sub_ring_at(base, h, 0);
        auto* entries = kickmsg::ring_entries(ring);
        uint64_t wp   = ring->write_pos.load(std::memory_order_acquire);
        if (wp > 0)
        {
            entries[(wp - 1) & h->sub_ring_mask].sequence = kickmsg::LOCKED_SEQUENCE;
        }
    }

    std::size_t repaired = region.repair_locked_entries();
    if (repaired != 1)
    {
        std::fprintf(stderr, "  [FAIL] repair_locked_entries returned %zu, expected 1\n", repaired);
        ok = false;
    }

    // Simulate an orphaned slot: pop one from the free stack (no ring references it)
    // and set its refcount > 0 as if a publisher crashed after refcount pre-set.
    {
        uint32_t idx = kickmsg::treiber_pop(h->free_top, base, h);
        auto* slot = kickmsg::slot_at(base, h, idx);
        slot->refcount = 3;
    }

    std::size_t reclaimed = region.reclaim_orphaned_slots();
    if (reclaimed != 1)
    {
        std::fprintf(stderr, "  [FAIL] reclaim_orphaned_slots returned %zu, expected 1\n", reclaimed);
        ok = false;
    }

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
