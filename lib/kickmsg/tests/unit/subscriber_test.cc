#include <gtest/gtest.h>

#include "kickmsg/Publisher.h"
#include "kickmsg/Subscriber.h"

#include <cstring>
#include <thread>

class SubscriberTest : public ::testing::Test
{
protected:
    static constexpr char const* SHM_NAME = "/kickmsg_test_subscriber";

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

TEST_F(SubscriberTest, ReceiveEmptyReturnsNullopt)
{
    auto cfg    = default_cfg();
    auto region = kickmsg::SharedRegion::create(SHM_NAME, kickmsg::ChannelType::PubSub, cfg);
    kickmsg::Subscriber sub(region);

    EXPECT_FALSE(sub.try_receive().has_value());
    EXPECT_FALSE(sub.try_receive_view().has_value());
}

TEST_F(SubscriberTest, ZeroCopyReceive)
{
    auto cfg    = default_cfg();
    auto region = kickmsg::SharedRegion::create(SHM_NAME, kickmsg::ChannelType::PubSub, cfg);

    kickmsg::Subscriber sub(region);
    kickmsg::Publisher  pub(region);

    uint64_t payload = 0x1234567890ABCDEFULL;
    ASSERT_GE(pub.send(&payload, sizeof(payload)), 0);

    auto view = sub.try_receive_view();
    ASSERT_TRUE(view.has_value());
    EXPECT_EQ(view->len(), sizeof(payload));

    uint64_t received = 0;
    std::memcpy(&received, view->data(), sizeof(received));
    EXPECT_EQ(received, payload);
}

TEST_F(SubscriberTest, SampleViewMoveSemantics)
{
    auto cfg    = default_cfg();
    auto region = kickmsg::SharedRegion::create(SHM_NAME, kickmsg::ChannelType::PubSub, cfg);

    kickmsg::Subscriber sub(region);
    kickmsg::Publisher  pub(region);

    uint32_t val = 99;
    ASSERT_GE(pub.send(&val, sizeof(val)), 0);

    auto view1 = sub.try_receive_view();
    ASSERT_TRUE(view1.has_value());

    auto view2 = std::move(*view1);
    EXPECT_EQ(view2.len(), sizeof(val));

    uint32_t got = 0;
    std::memcpy(&got, view2.data(), sizeof(got));
    EXPECT_EQ(got, 99u);

    kickmsg::Subscriber::SampleView view3;
    view3 = std::move(view2);
    EXPECT_EQ(view3.len(), sizeof(val));
}

TEST_F(SubscriberTest, OverwritingRingReportsLoss)
{
    kickmsg::RingConfig cfg;
    cfg.max_subscribers   = 1;
    cfg.sub_ring_capacity = 4;
    cfg.pool_size         = 8;
    cfg.max_payload_size  = 8;

    auto region = kickmsg::SharedRegion::create(SHM_NAME, kickmsg::ChannelType::PubSub, cfg);

    kickmsg::Subscriber sub(region);
    kickmsg::Publisher  pub(region);

    for (int i = 0; i < 16; ++i)
    {
        pub.send(&i, sizeof(i));
    }

    int received = 0;
    while (auto sample = sub.try_receive())
    {
        ++received;
    }

    EXPECT_GT(sub.lost(), 0u);
    EXPECT_LT(static_cast<uint64_t>(received), 16u);
}

TEST_F(SubscriberTest, DrainReleasesSlots)
{
    kickmsg::RingConfig cfg;
    cfg.max_subscribers   = 2;
    cfg.sub_ring_capacity = 8;
    cfg.pool_size         = 16;
    cfg.max_payload_size  = 64;

    auto region = kickmsg::SharedRegion::create(
                      SHM_NAME, kickmsg::ChannelType::PubSub, cfg);
    kickmsg::Publisher pub(region);

    auto count_free = [&]()
    {
        uint32_t count = 0;
        auto* hdr = region.header();
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

    {
        kickmsg::Subscriber sub(region);
        auto free_before = count_free();

        for (int i = 0; i < 5; ++i)
        {
            uint32_t val = static_cast<uint32_t>(i);
            ASSERT_GE(pub.send(&val, sizeof(val)), 0);
        }

        EXPECT_EQ(count_free(), free_before - 5);
    }

    EXPECT_EQ(count_free(), 16u);
}

TEST_F(SubscriberTest, BlockingReceiveTimesOut)
{
    auto cfg    = default_cfg();
    auto region = kickmsg::SharedRegion::create(SHM_NAME, kickmsg::ChannelType::PubSub, cfg);
    kickmsg::Subscriber sub(region);

    auto start  = std::chrono::steady_clock::now();
    auto sample = sub.receive(std::chrono::milliseconds{50});
    auto elapsed = std::chrono::steady_clock::now() - start;

    EXPECT_FALSE(sample.has_value());
    EXPECT_GE(elapsed, std::chrono::milliseconds{40});
}

TEST_F(SubscriberTest, BlockingReceiveWakesOnPublish)
{
    auto cfg    = default_cfg();
    auto region = kickmsg::SharedRegion::create(SHM_NAME, kickmsg::ChannelType::PubSub, cfg);

    kickmsg::Subscriber sub(region);
    kickmsg::Publisher  pub(region);

    std::thread sender([&]()
    {
        std::this_thread::sleep_for(std::chrono::milliseconds{20});
        uint32_t val = 123;
        pub.send(&val, sizeof(val));
    });

    auto sample = sub.receive(std::chrono::seconds{2});
    ASSERT_TRUE(sample.has_value());

    uint32_t got = 0;
    std::memcpy(&got, sample->data(), sizeof(got));
    EXPECT_EQ(got, 123u);

    sender.join();
}
