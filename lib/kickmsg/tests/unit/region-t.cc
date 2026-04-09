#include <gtest/gtest.h>

#include "kickmsg/Region.h"
#include "kickmsg/Publisher.h"
#include "kickmsg/Subscriber.h"

#include <cstring>
#include <unistd.h>

class RegionTest : public ::testing::Test
{
public:
    static constexpr char const* SHM_NAME = "/kickmsg_test_region";

    void SetUp() override
    {
        kickmsg::SharedMemory::unlink(SHM_NAME);
    }

    void TearDown() override
    {
        kickmsg::SharedMemory::unlink(SHM_NAME);
    }

    kickmsg::channel::Config default_cfg()
    {
        kickmsg::channel::Config cfg;
        cfg.max_subscribers   = 4;
        cfg.sub_ring_capacity = 8;
        cfg.pool_size         = 16;
        cfg.max_payload_size  = 64;
        return cfg;
    }
};

TEST_F(RegionTest, CreateAndValidateHeader)
{
    auto cfg    = default_cfg();
    auto region = kickmsg::SharedRegion::create(SHM_NAME, kickmsg::channel::PubSub, cfg, "test");
    auto* hdr   = region.header();

    EXPECT_EQ(hdr->magic, kickmsg::MAGIC);
    EXPECT_EQ(hdr->version, kickmsg::VERSION);
    EXPECT_EQ(hdr->channel_type, kickmsg::channel::PubSub);
    EXPECT_EQ(hdr->max_subs, cfg.max_subscribers);
    EXPECT_EQ(hdr->sub_ring_capacity, cfg.sub_ring_capacity);
    EXPECT_EQ(hdr->sub_ring_mask, cfg.sub_ring_capacity - 1);
    EXPECT_EQ(hdr->pool_size, cfg.pool_size);
    EXPECT_EQ(hdr->slot_data_size, cfg.max_payload_size);
    EXPECT_EQ(hdr->creator_name_len, 4u);

    std::string creator(kickmsg::header_creator_name(hdr), hdr->creator_name_len);
    EXPECT_EQ(creator, "test");
}

TEST_F(RegionTest, OpenExistingRegion)
{
    auto cfg = default_cfg();
    auto r1  = kickmsg::SharedRegion::create(SHM_NAME, kickmsg::channel::PubSub, cfg, "orig");
    auto r2  = kickmsg::SharedRegion::open(SHM_NAME);

    EXPECT_EQ(r2.header()->magic, kickmsg::MAGIC);
    EXPECT_EQ(r2.header()->pool_size, cfg.pool_size);
}

TEST_F(RegionTest, OpenNonexistentThrows)
{
    EXPECT_THROW(kickmsg::SharedRegion::open("/kickmsg_nonexistent_42"), std::runtime_error);
}

TEST_F(RegionTest, CreateOrOpenFirstCreates)
{
    auto cfg = default_cfg();
    auto r   = kickmsg::SharedRegion::create_or_open(
                   SHM_NAME, kickmsg::channel::Broadcast, cfg, "creator");

    EXPECT_EQ(r.header()->magic, kickmsg::MAGIC);
    EXPECT_EQ(r.header()->channel_type, kickmsg::channel::Broadcast);
}

TEST_F(RegionTest, CreateOrOpenSecondOpens)
{
    auto cfg = default_cfg();
    auto r1  = kickmsg::SharedRegion::create_or_open(
                   SHM_NAME, kickmsg::channel::Broadcast, cfg, "first");
    auto r2  = kickmsg::SharedRegion::create_or_open(
                   SHM_NAME, kickmsg::channel::Broadcast, cfg, "second");

    EXPECT_EQ(r2.header()->magic, kickmsg::MAGIC);
    EXPECT_EQ(r2.header()->pool_size, cfg.pool_size);
}

TEST_F(RegionTest, CreateOrOpenConfigMismatchThrows)
{
    auto cfg = default_cfg();
    kickmsg::SharedRegion::create(SHM_NAME, kickmsg::channel::PubSub, cfg, "node");

    auto bad_cfg = cfg;
    bad_cfg.max_payload_size = cfg.max_payload_size * 2;
    EXPECT_THROW(
        kickmsg::SharedRegion::create_or_open(
            SHM_NAME, kickmsg::channel::PubSub, bad_cfg, "other"),
        std::runtime_error);
}

TEST_F(RegionTest, HeaderStoresCreatorMetadata)
{
    auto cfg    = default_cfg();
    auto region = kickmsg::SharedRegion::create(
                      SHM_NAME, kickmsg::channel::PubSub, cfg, "my_node");
    auto* hdr   = region.header();

    EXPECT_EQ(hdr->creator_pid, static_cast<uint64_t>(getpid()));
    EXPECT_GT(hdr->created_at_ns, 0u);
    EXPECT_NE(hdr->config_hash, 0u);
}

TEST_F(RegionTest, NonPowerOfTwoRingThrows)
{
    auto cfg = default_cfg();
    cfg.sub_ring_capacity = 7;
    EXPECT_THROW(
        kickmsg::SharedRegion::create(SHM_NAME, kickmsg::channel::PubSub, cfg),
        std::runtime_error);
}

TEST_F(RegionTest, TreiberPopAllThenPushBack)
{
    auto cfg    = default_cfg();
    auto region = kickmsg::SharedRegion::create(SHM_NAME, kickmsg::channel::PubSub, cfg);
    auto* base  = region.base();
    auto* hdr   = region.header();

    std::vector<uint32_t> popped;
    for (uint32_t i = 0; i < cfg.pool_size; ++i)
    {
        uint32_t idx = kickmsg::treiber_pop(hdr->free_top, base, hdr);
        ASSERT_NE(idx, kickmsg::INVALID_SLOT) << "Pop failed at iteration " << i;
        popped.push_back(idx);
    }

    EXPECT_EQ(kickmsg::treiber_pop(hdr->free_top, base, hdr), kickmsg::INVALID_SLOT);

    for (auto idx : popped)
    {
        auto* slot = kickmsg::slot_at(base, hdr, idx);
        kickmsg::treiber_push(hdr->free_top, slot, idx);
    }

    uint32_t count = 0;
    uint32_t top = kickmsg::tagged_idx(hdr->free_top.load(std::memory_order_acquire));
    while (top != kickmsg::INVALID_SLOT)
    {
        auto* slot = kickmsg::slot_at(base, hdr, top);
        top = slot->next_free;
        ++count;
    }
    EXPECT_EQ(count, static_cast<uint32_t>(cfg.pool_size));
}

TEST_F(RegionTest, CollectGarbageReclaimsOrphanedSlots)
{
    kickmsg::channel::Config cfg;
    cfg.max_subscribers   = 2;
    cfg.sub_ring_capacity = 8;
    cfg.pool_size         = 16;
    cfg.max_payload_size  = 64;

    auto region = kickmsg::SharedRegion::create(
                      SHM_NAME, kickmsg::channel::PubSub, cfg);
    auto* hdr = region.header();

    auto count_free = [&]()
    {
        uint32_t count = 0;
        uint64_t top = hdr->free_top.load(std::memory_order_acquire);
        uint32_t idx = kickmsg::tagged_idx(top);
        while (idx != kickmsg::INVALID_SLOT)
        {
            auto* slot = kickmsg::slot_at(region.base(), hdr, idx);
            idx = slot->next_free;
            ++count;
        }
        return count;
    };

    EXPECT_EQ(count_free(), 16u);

    for (int i = 0; i < 3; ++i)
    {
        uint32_t idx = kickmsg::treiber_pop(hdr->free_top, region.base(), hdr);
        ASSERT_NE(idx, kickmsg::INVALID_SLOT);
        auto* slot = kickmsg::slot_at(region.base(), hdr, idx);
        slot->refcount.store(static_cast<uint32_t>(cfg.max_subscribers),
                             std::memory_order_release);
    }

    EXPECT_EQ(count_free(), 13u);

    std::size_t reclaimed = region.reclaim_orphaned_slots();
    EXPECT_EQ(reclaimed, 3u);
    EXPECT_EQ(count_free(), 16u);

    EXPECT_EQ(region.reclaim_orphaned_slots(), 0u);
}

TEST_F(RegionTest, RepairLockedEntryUnblocksPublishing)
{
    // Verify that after repair_locked_entries(), the repaired ring position
    // can be published over again when the ring wraps.

    kickmsg::channel::Config cfg;
    cfg.max_subscribers   = 1;
    cfg.sub_ring_capacity = 4;    // capacity = 4, so pos=4 wraps to idx=0
    cfg.pool_size         = 16;
    cfg.max_payload_size  = 8;

    auto region = kickmsg::SharedRegion::create(SHM_NAME, kickmsg::channel::PubSub, cfg);

    kickmsg::Subscriber sub(region);
    kickmsg::Publisher  pub(region);

    // Publish 1 message at pos=0, creating a committed entry with seq=1
    uint32_t val = 100;
    ASSERT_GE(pub.send(&val, sizeof(val)), 0);

    // Consume it so the subscriber is caught up
    auto sample = sub.try_receive();
    ASSERT_TRUE(sample.has_value());

    // Simulate a crash at pos=1: manually lock the entry
    auto* ring    = kickmsg::sub_ring_at(region.base(), region.header(), 0);
    auto* entries = kickmsg::ring_entries(ring);

    // Advance write_pos to simulate that a publisher claimed pos=1
    ring->write_pos.store(2, std::memory_order_release);
    auto& e1 = entries[1]; // pos=1 → idx=1
    e1.sequence.store(kickmsg::LOCKED_SEQUENCE, std::memory_order_release);

    // Repair should fix the locked entry
    std::size_t repaired = region.repair_locked_entries();
    EXPECT_EQ(repaired, 1u);

    // The repaired entry should have seq = pos + 1 = 2
    uint64_t seq = e1.sequence.load(std::memory_order_acquire);
    EXPECT_EQ(seq, 2u);

    // The repaired entry should have INVALID_SLOT
    uint32_t slot_idx = e1.slot_idx.load(std::memory_order_acquire);
    EXPECT_EQ(slot_idx, kickmsg::INVALID_SLOT);

    // Now publish enough to wrap around: pos 2, 3, 4, 5
    // pos=4 wraps to idx=0 and expects prev_seq=1 (pos 0's committed seq) — OK
    // pos=5 wraps to idx=1 and expects prev_seq=2 (the repaired seq) — this
    // would fail with the old code that stored prev_seq instead of pos+1
    for (int i = 0; i < 4; ++i)
    {
        val = static_cast<uint32_t>(200 + i);
        ASSERT_GE(pub.send(&val, sizeof(val)), 0)
            << "Publishing failed at iteration " << i
            << " — repaired entry likely blocked the ring";
    }

    // Subscriber should receive the new messages (some may be lost due to wrapping)
    int received = 0;
    while (auto s = sub.try_receive())
    {
        ++received;
    }
    EXPECT_GT(received, 0);
}

TEST_F(RegionTest, RepairLockedEntryAtPositionZero)
{
    // Edge case: crash at pos=0 where prev_seq was 0.
    // Old code stored prev_seq=0, new code stores pos+1=1.

    kickmsg::channel::Config cfg;
    cfg.max_subscribers   = 1;
    cfg.sub_ring_capacity = 4;
    cfg.pool_size         = 8;
    cfg.max_payload_size  = 8;

    auto region = kickmsg::SharedRegion::create(SHM_NAME, kickmsg::channel::PubSub, cfg);

    kickmsg::Subscriber sub(region);

    // Simulate crash at pos=0: lock the entry, advance write_pos
    auto* ring    = kickmsg::sub_ring_at(region.base(), region.header(), 0);
    auto* entries = kickmsg::ring_entries(ring);
    ring->write_pos.store(1, std::memory_order_release);
    entries[0].sequence.store(kickmsg::LOCKED_SEQUENCE, std::memory_order_release);

    std::size_t repaired = region.repair_locked_entries();
    EXPECT_EQ(repaired, 1u);
    EXPECT_EQ(entries[0].sequence.load(std::memory_order_acquire), 1u);
    EXPECT_EQ(entries[0].slot_idx.load(std::memory_order_acquire), kickmsg::INVALID_SLOT);

    // Publishing should work: pos=1,2,3 use fresh indices, pos=4 wraps to idx=0
    // and expects prev_seq=1 — matches the repaired value
    kickmsg::Publisher pub(region);
    for (int i = 0; i < 5; ++i)
    {
        uint32_t val = static_cast<uint32_t>(i);
        ASSERT_GE(pub.send(&val, sizeof(val)), 0)
            << "Publishing failed at iteration " << i;
    }
}

TEST_F(RegionTest, DiagnoseHealthyReturnsZeros)
{
    auto cfg    = default_cfg();
    auto region = kickmsg::SharedRegion::create(SHM_NAME, kickmsg::channel::PubSub, cfg);

    kickmsg::Subscriber sub(region);
    kickmsg::Publisher  pub(region);

    for (int i = 0; i < 5; ++i)
    {
        uint32_t val = static_cast<uint32_t>(i);
        ASSERT_GE(pub.send(&val, sizeof(val)), 0);
    }

    auto report = region.diagnose();
    EXPECT_EQ(report.locked_entries, 0u);
    EXPECT_EQ(report.retired_rings, 0u);
}

TEST_F(RegionTest, DiagnoseDetectsLockedEntries)
{
    kickmsg::channel::Config cfg;
    cfg.max_subscribers   = 1;
    cfg.sub_ring_capacity = 4;
    cfg.pool_size         = 8;
    cfg.max_payload_size  = 8;

    auto region = kickmsg::SharedRegion::create(SHM_NAME, kickmsg::channel::PubSub, cfg);

    kickmsg::Subscriber sub(region);
    kickmsg::Publisher  pub(region);

    // Publish one normal message
    uint32_t val = 1;
    ASSERT_GE(pub.send(&val, sizeof(val)), 0);

    // Simulate a crashed publisher at pos=1: lock the entry
    auto* ring    = kickmsg::sub_ring_at(region.base(), region.header(), 0);
    auto* entries = kickmsg::ring_entries(ring);
    ring->write_pos.store(2, std::memory_order_release);
    entries[1].sequence.store(kickmsg::LOCKED_SEQUENCE, std::memory_order_release);

    auto report = region.diagnose();
    EXPECT_EQ(report.locked_entries, 1u);
    EXPECT_EQ(report.retired_rings, 0u);

    // Repair and verify clean
    region.repair_locked_entries();
    report = region.diagnose();
    EXPECT_EQ(report.locked_entries, 0u);
}

TEST_F(RegionTest, DiagnoseDetectsStuckRings)
{
    kickmsg::channel::Config cfg;
    cfg.max_subscribers   = 2;
    cfg.sub_ring_capacity = 4;
    cfg.pool_size         = 8;
    cfg.max_payload_size  = 8;

    auto region = kickmsg::SharedRegion::create(SHM_NAME, kickmsg::channel::PubSub, cfg);

    // Simulate a stuck ring: Free with stale in_flight
    auto* ring = kickmsg::sub_ring_at(region.base(), region.header(), 0);
    ring->state_flight.store(
        kickmsg::ring::make_packed(kickmsg::ring::Free, 1),
        std::memory_order_release);

    auto report = region.diagnose();
    EXPECT_EQ(report.locked_entries, 0u);
    EXPECT_EQ(report.retired_rings, 1u);

    // Reset retired rings and verify clean
    std::size_t reset = region.reset_retired_rings();
    EXPECT_EQ(reset, 1u);
    report = region.diagnose();
    EXPECT_EQ(report.retired_rings, 0u);

    // Subscriber can now join the recovered ring
    kickmsg::Subscriber sub(region);
    kickmsg::Publisher  pub(region);

    uint32_t val = 42;
    ASSERT_GE(pub.send(&val, sizeof(val)), 0);
    auto sample = sub.try_receive();
    ASSERT_TRUE(sample.has_value());

    uint32_t got = 0;
    std::memcpy(&got, sample->data(), sizeof(got));
    EXPECT_EQ(got, 42u);
}

TEST_F(RegionTest, ResetRetiredRingsLeavesDrainingUntouched)
{
    kickmsg::channel::Config cfg;
    cfg.max_subscribers   = 2;
    cfg.sub_ring_capacity = 4;
    cfg.pool_size         = 8;
    cfg.max_payload_size  = 8;

    auto region = kickmsg::SharedRegion::create(SHM_NAME, kickmsg::channel::PubSub, cfg);

    // Ring 0: retired (Free | in_flight=1) — should be reset
    auto* ring0 = kickmsg::sub_ring_at(region.base(), region.header(), 0);
    ring0->state_flight.store(
        kickmsg::ring::make_packed(kickmsg::ring::Free, 1),
        std::memory_order_release);

    // Ring 1: draining (Draining | in_flight=1) — must NOT be touched
    auto* ring1 = kickmsg::sub_ring_at(region.base(), region.header(), 1);
    ring1->state_flight.store(
        kickmsg::ring::make_packed(kickmsg::ring::Draining, 1),
        std::memory_order_release);

    auto report = region.diagnose();
    EXPECT_EQ(report.retired_rings, 1u);
    EXPECT_EQ(report.draining_rings, 1u);

    std::size_t reset = region.reset_retired_rings();
    EXPECT_EQ(reset, 1u);  // only the retired ring

    // Ring 0 was reset
    uint32_t packed0 = ring0->state_flight.load(std::memory_order_acquire);
    EXPECT_EQ(packed0, kickmsg::ring::make_packed(kickmsg::ring::Free));

    // Ring 1 is still Draining with in_flight preserved
    uint32_t packed1 = ring1->state_flight.load(std::memory_order_acquire);
    EXPECT_EQ(kickmsg::ring::get_state(packed1), kickmsg::ring::Draining);
    EXPECT_EQ(kickmsg::ring::get_in_flight(packed1), 1u);
}

TEST_F(RegionTest, CollectGarbageDoesNotReclaimLiveSlots)
{
    kickmsg::channel::Config cfg;
    cfg.max_subscribers   = 2;
    cfg.sub_ring_capacity = 8;
    cfg.pool_size         = 16;
    cfg.max_payload_size  = 64;

    auto region = kickmsg::SharedRegion::create(SHM_NAME, kickmsg::channel::PubSub, cfg);

    kickmsg::Subscriber sub(region);
    kickmsg::Publisher  pub(region);

    for (int i = 0; i < 4; ++i)
    {
        uint32_t val = static_cast<uint32_t>(i);
        ASSERT_GE(pub.send(&val, sizeof(val)), 0);
    }

    std::size_t reclaimed = region.reclaim_orphaned_slots();
    EXPECT_EQ(reclaimed, 0u);

    for (int i = 0; i < 4; ++i)
    {
        auto msg = sub.try_receive();
        ASSERT_TRUE(msg.has_value());
    }

    EXPECT_EQ(region.reclaim_orphaned_slots(), 0u);
}
