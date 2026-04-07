#include <gtest/gtest.h>

#include "kickmsg/Region.h"
#include "kickmsg/Publisher.h"
#include "kickmsg/Subscriber.h"

#include <cstring>
#include <unistd.h>

class RegionTest : public ::testing::Test
{
protected:
    static constexpr char const* SHM_NAME = "/kickmsg_test_region";

    void SetUp() override
    {
        kickmsg::SharedMemory::unlink(SHM_NAME);
    }

    void TearDown() override
    {
        kickmsg::SharedMemory::unlink(SHM_NAME);
    }

    kickmsg::RingConfig default_cfg()
    {
        kickmsg::RingConfig cfg;
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
    auto region = kickmsg::SharedRegion::create(SHM_NAME, kickmsg::ChannelType::PubSub, cfg, "test");
    auto* hdr   = region.header();

    EXPECT_EQ(hdr->magic, kickmsg::MAGIC);
    EXPECT_EQ(hdr->version, kickmsg::VERSION);
    EXPECT_EQ(hdr->channel_type, kickmsg::ChannelType::PubSub);
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
    auto r1  = kickmsg::SharedRegion::create(SHM_NAME, kickmsg::ChannelType::PubSub, cfg, "orig");
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
                   SHM_NAME, kickmsg::ChannelType::Broadcast, cfg, "creator");

    EXPECT_EQ(r.header()->magic, kickmsg::MAGIC);
    EXPECT_EQ(r.header()->channel_type, kickmsg::ChannelType::Broadcast);
}

TEST_F(RegionTest, CreateOrOpenSecondOpens)
{
    auto cfg = default_cfg();
    auto r1  = kickmsg::SharedRegion::create_or_open(
                   SHM_NAME, kickmsg::ChannelType::Broadcast, cfg, "first");
    auto r2  = kickmsg::SharedRegion::create_or_open(
                   SHM_NAME, kickmsg::ChannelType::Broadcast, cfg, "second");

    EXPECT_EQ(r2.header()->magic, kickmsg::MAGIC);
    EXPECT_EQ(r2.header()->pool_size, cfg.pool_size);
}

TEST_F(RegionTest, CreateOrOpenConfigMismatchThrows)
{
    auto cfg = default_cfg();
    kickmsg::SharedRegion::create(SHM_NAME, kickmsg::ChannelType::PubSub, cfg, "node");

    auto bad_cfg = cfg;
    bad_cfg.max_payload_size = cfg.max_payload_size * 2;
    EXPECT_THROW(
        kickmsg::SharedRegion::create_or_open(
            SHM_NAME, kickmsg::ChannelType::PubSub, bad_cfg, "other"),
        std::runtime_error);
}

TEST_F(RegionTest, HeaderStoresCreatorMetadata)
{
    auto cfg    = default_cfg();
    auto region = kickmsg::SharedRegion::create(
                      SHM_NAME, kickmsg::ChannelType::PubSub, cfg, "my_node");
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
        kickmsg::SharedRegion::create(SHM_NAME, kickmsg::ChannelType::PubSub, cfg),
        std::runtime_error);
}

TEST_F(RegionTest, TreiberPopAllThenPushBack)
{
    auto cfg    = default_cfg();
    auto region = kickmsg::SharedRegion::create(SHM_NAME, kickmsg::ChannelType::PubSub, cfg);
    auto* base  = region.base();
    auto* hdr   = region.header();

    std::vector<uint32_t> popped;
    for (uint32_t i = 0; i < cfg.pool_size; ++i)
    {
        auto idx = kickmsg::treiber_pop(hdr->free_top, base, hdr);
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
    auto top = kickmsg::tagged_idx(hdr->free_top.load(std::memory_order_acquire));
    while (top != kickmsg::INVALID_SLOT)
    {
        auto* slot = kickmsg::slot_at(base, hdr, top);
        top = slot->next_free.load(std::memory_order_relaxed);
        ++count;
    }
    EXPECT_EQ(count, static_cast<uint32_t>(cfg.pool_size));
}

TEST_F(RegionTest, CollectGarbageReclaimsOrphanedSlots)
{
    kickmsg::RingConfig cfg;
    cfg.max_subscribers   = 2;
    cfg.sub_ring_capacity = 8;
    cfg.pool_size         = 16;
    cfg.max_payload_size  = 64;

    auto region = kickmsg::SharedRegion::create(
                      SHM_NAME, kickmsg::ChannelType::PubSub, cfg);
    auto* hdr = region.header();

    auto count_free = [&]()
    {
        uint32_t count = 0;
        auto  top = hdr->free_top.load(std::memory_order_acquire);
        auto  idx = kickmsg::tagged_idx(top);
        while (idx != kickmsg::INVALID_SLOT)
        {
            auto* slot = kickmsg::slot_at(region.base(), hdr, idx);
            idx = slot->next_free.load(std::memory_order_relaxed);
            ++count;
        }
        return count;
    };

    EXPECT_EQ(count_free(), 16u);

    for (int i = 0; i < 3; ++i)
    {
        auto idx = kickmsg::treiber_pop(hdr->free_top, region.base(), hdr);
        ASSERT_NE(idx, kickmsg::INVALID_SLOT);
        auto* slot = kickmsg::slot_at(region.base(), hdr, idx);
        slot->refcount.store(static_cast<uint32_t>(cfg.max_subscribers),
                             std::memory_order_release);
    }

    EXPECT_EQ(count_free(), 13u);

    auto reclaimed = region.reclaim_orphaned_slots();
    EXPECT_EQ(reclaimed, 3u);
    EXPECT_EQ(count_free(), 16u);

    EXPECT_EQ(region.reclaim_orphaned_slots(), 0u);
}

TEST_F(RegionTest, CollectGarbageDoesNotReclaimLiveSlots)
{
    kickmsg::RingConfig cfg;
    cfg.max_subscribers   = 2;
    cfg.sub_ring_capacity = 8;
    cfg.pool_size         = 16;
    cfg.max_payload_size  = 64;

    auto region = kickmsg::SharedRegion::create(
                      SHM_NAME, kickmsg::ChannelType::PubSub, cfg);

    kickmsg::Subscriber sub(region);
    kickmsg::Publisher  pub(region);

    for (int i = 0; i < 4; ++i)
    {
        uint32_t val = static_cast<uint32_t>(i);
        ASSERT_TRUE(pub.send(&val, sizeof(val)));
    }

    auto reclaimed = region.reclaim_orphaned_slots();
    EXPECT_EQ(reclaimed, 0u);

    for (int i = 0; i < 4; ++i)
    {
        auto msg = sub.try_receive();
        ASSERT_TRUE(msg.has_value());
    }

    EXPECT_EQ(region.reclaim_orphaned_slots(), 0u);
}
