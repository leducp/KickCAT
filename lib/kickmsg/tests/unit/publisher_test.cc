#include <gtest/gtest.h>

#include "kickmsg/Publisher.h"
#include "kickmsg/Subscriber.h"

#include <cstring>

class PublisherTest : public ::testing::Test
{
protected:
    static constexpr char const* SHM_NAME = "/kickmsg_test_publisher";

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

TEST_F(PublisherTest, SendReceiveSingleMessage)
{
    auto cfg    = default_cfg();
    auto region = kickmsg::SharedRegion::create(SHM_NAME, kickmsg::ChannelType::PubSub, cfg);

    kickmsg::Subscriber sub(region);
    kickmsg::Publisher  pub(region);

    uint64_t payload = 0xDEADBEEFCAFEBABEULL;
    ASSERT_TRUE(pub.send(&payload, sizeof(payload)));

    auto sample = sub.try_receive();
    ASSERT_TRUE(sample.has_value());
    EXPECT_EQ(sample->len(), sizeof(payload));

    uint64_t received = 0;
    std::memcpy(&received, sample->data(), sizeof(received));
    EXPECT_EQ(received, payload);
}

TEST_F(PublisherTest, AllocatePublishSeparately)
{
    auto cfg    = default_cfg();
    auto region = kickmsg::SharedRegion::create(SHM_NAME, kickmsg::ChannelType::PubSub, cfg);

    kickmsg::Subscriber sub(region);
    kickmsg::Publisher  pub(region);

    auto* ptr = pub.allocate(sizeof(uint32_t));
    ASSERT_NE(ptr, nullptr);

    uint32_t val = 42;
    std::memcpy(ptr, &val, sizeof(val));
    auto delivered = pub.publish();
    EXPECT_EQ(delivered, 1u);

    auto sample = sub.try_receive();
    ASSERT_TRUE(sample.has_value());

    uint32_t got = 0;
    std::memcpy(&got, sample->data(), sizeof(got));
    EXPECT_EQ(got, 42u);
}

TEST_F(PublisherTest, AllocateTooLargeReturnsNull)
{
    auto cfg    = default_cfg();
    auto region = kickmsg::SharedRegion::create(SHM_NAME, kickmsg::ChannelType::PubSub, cfg);
    kickmsg::Publisher pub(region);

    EXPECT_EQ(pub.allocate(cfg.max_payload_size + 1), nullptr);
}

TEST_F(PublisherTest, MultipleMessages)
{
    auto cfg    = default_cfg();
    auto region = kickmsg::SharedRegion::create(SHM_NAME, kickmsg::ChannelType::PubSub, cfg);

    kickmsg::Subscriber sub(region);
    kickmsg::Publisher  pub(region);

    constexpr int N = 5;
    for (int i = 0; i < N; ++i)
    {
        ASSERT_TRUE(pub.send(&i, sizeof(i)));
    }

    for (int i = 0; i < N; ++i)
    {
        auto sample = sub.try_receive();
        ASSERT_TRUE(sample.has_value()) << "Missing message at i=" << i;

        int got = 0;
        std::memcpy(&got, sample->data(), sizeof(got));
        EXPECT_EQ(got, i);
    }
}

TEST_F(PublisherTest, MultipleSubscribersEachReceive)
{
    auto cfg    = default_cfg();
    auto region = kickmsg::SharedRegion::create(SHM_NAME, kickmsg::ChannelType::PubSub, cfg);

    kickmsg::Subscriber sub1(region);
    kickmsg::Subscriber sub2(region);
    kickmsg::Publisher   pub(region);

    uint32_t val = 7;
    ASSERT_TRUE(pub.send(&val, sizeof(val)));

    auto s1 = sub1.try_receive();
    auto s2 = sub2.try_receive();

    ASSERT_TRUE(s1.has_value());
    ASSERT_TRUE(s2.has_value());

    uint32_t r1 = 0, r2 = 0;
    std::memcpy(&r1, s1->data(), sizeof(r1));
    std::memcpy(&r2, s2->data(), sizeof(r2));

    EXPECT_EQ(r1, 7u);
    EXPECT_EQ(r2, 7u);
}
