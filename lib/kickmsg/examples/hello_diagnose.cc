/// @file hello_diagnose.cc
/// @brief KickMsg health diagnostics example with fault injection.
///
/// Demonstrates the diagnose() / repair_locked_entries() recovery workflow:
///   1. Create a healthy channel, verify diagnose() reports zero issues
///   2. Inject faults (locked entry + stuck ring) simulating publisher crashes
///   3. Detect damage with diagnose()
///   4. Repair with repair_locked_entries()
///   5. Verify the channel is fully operational again

#include <cstdint>
#include <cstring>
#include <iostream>

#include <kickmsg/Publisher.h>
#include <kickmsg/Subscriber.h>

void print_report(char const* label, kickmsg::SharedRegion::HealthReport const& report)
{
    std::cout << "[" << label << "] "
              << "locked_entries=" << report.locked_entries
              << " retired_rings=" << report.retired_rings
              << " draining_rings=" << report.draining_rings
              << " live_rings=" << report.live_rings
              << "\n";
}

int main()
{
    char const* SHM_NAME = "/kickmsg_hello_diagnose";
    kickmsg::SharedMemory::unlink(SHM_NAME);

    kickmsg::channel::Config cfg;
    cfg.max_subscribers   = 4;
    cfg.sub_ring_capacity = 8;
    cfg.pool_size         = 32;
    cfg.max_payload_size  = 64;

    auto region = kickmsg::SharedRegion::create(
        SHM_NAME, kickmsg::channel::PubSub, cfg, "diagnose_example");

    // --- Step 1: Healthy channel ---
    std::cout << "=== Step 1: Healthy channel ===\n";
    {
        kickmsg::Subscriber sub(region);
        kickmsg::Publisher  pub(region);

        for (uint32_t i = 0; i < 5; ++i)
        {
            pub.send(&i, sizeof(i));
        }

        auto report = region.diagnose();
        print_report("healthy", report);
    }

    // --- Step 2: Inject faults ---
    std::cout << "\n=== Step 2: Inject faults (simulating publisher crashes) ===\n";

    auto* base = region.base();
    auto* hdr  = region.header();

    // Fault 1: Lock a ring entry (simulates publisher crash mid-commit)
    {
        auto* ring    = kickmsg::sub_ring_at(base, hdr, 0);
        auto* entries = kickmsg::ring_entries(ring);
        // Pretend a publisher claimed pos=write_pos and locked the entry
        uint64_t wp = ring->write_pos.load(std::memory_order_acquire);
        ring->write_pos.store(wp + 1, std::memory_order_release);
        entries[wp & hdr->sub_ring_mask].sequence.store(
            kickmsg::LOCKED_SEQUENCE, std::memory_order_release);
        std::cout << "  Injected: LOCKED_SEQUENCE at ring 0, pos " << wp << "\n";
    }

    // Fault 2: Stuck ring (simulates subscriber teardown timeout after publisher crash)
    {
        auto* ring = kickmsg::sub_ring_at(base, hdr, 1);
        ring->state_flight.store(
            kickmsg::ring::make_packed(kickmsg::ring::Free, 1),
            std::memory_order_release);
        std::cout << "  Injected: ring 1 stuck at Free | in_flight=1\n";
    }

    // --- Step 3: Detect damage ---
    std::cout << "\n=== Step 3: Diagnose ===\n";
    auto report = region.diagnose();
    print_report("after faults", report);

    if (report.locked_entries > 0 or report.retired_rings > 0)
    {
        std::cout << "  -> Damage detected! Recovery needed.\n";
    }

    // --- Step 4: Repair ---
    std::cout << "\n=== Step 4: Repair ===\n";

    // Step 4a: repair locked entries (safe under live traffic)
    std::size_t repaired = region.repair_locked_entries();
    std::cout << "  repair_locked_entries() fixed " << repaired << " locked entries\n";

    // Step 4b: reset retired rings (only after confirming publisher is gone)
    std::size_t reset = region.reset_retired_rings();
    std::cout << "  reset_retired_rings() recovered " << reset << " stuck rings\n";

    report = region.diagnose();
    print_report("after repair", report);

    // --- Step 5: Verify channel is operational ---
    std::cout << "\n=== Step 5: Verify channel works after repair ===\n";
    {
        kickmsg::Subscriber sub(region);
        kickmsg::Publisher  pub(region);

        uint32_t sent = 0;
        for (uint32_t i = 100; i < 105; ++i)
        {
            if (pub.send(&i, sizeof(i)) >= 0)
            {
                ++sent;
            }
        }
        std::cout << "  Sent " << sent << " messages\n";

        uint32_t received = 0;
        while (auto sample = sub.try_receive())
        {
            uint32_t val = 0;
            std::memcpy(&val, sample->data(), sizeof(val));
            std::cout << "  Received: " << val << "\n";
            ++received;
        }
        std::cout << "  Total received: " << received << "/" << sent << "\n";
    }

    region.unlink();
    std::cout << "\nDone.\n";
    return 0;
}
