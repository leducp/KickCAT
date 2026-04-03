#include <gtest/gtest.h>

#include "kickmsg/Node.h"

#include <cstring>
#include <string>

class NodeTest : public ::testing::Test
{
protected:
    void TearDown() override
    {
        for (auto const& name : shm_names_)
        {
            kickmsg::SharedMemory::unlink(name);
        }
    }

    void track(std::string name)
    {
        shm_names_.push_back(std::move(name));
    }

    kickmsg::RingConfig small_cfg()
    {
        kickmsg::RingConfig cfg;
        cfg.max_subscribers   = 4;
        cfg.sub_ring_capacity = 8;
        cfg.pool_size         = 16;
        cfg.max_payload_size  = 64;
        return cfg;
    }

private:
    std::vector<std::string> shm_names_;
};

TEST_F(NodeTest, AdvertiseAndSubscribe)
{
    // Topic-centric: SHM name is /{prefix}_{topic}, no node name in path
    track("/test_data");

    kickmsg::Node pub_node("pubnode", "test");
    auto pub = pub_node.advertise("data", small_cfg());

    // Any node can subscribe by topic name alone
    kickmsg::Node sub_node("subnode", "test");
    auto sub = sub_node.subscribe("data");

    uint32_t val = 42;
    ASSERT_TRUE(pub.send(&val, sizeof(val)));

    auto sample = sub.try_receive();
    ASSERT_TRUE(sample.has_value());

    uint32_t got = 0;
    std::memcpy(&got, sample->data(), sizeof(got));
    EXPECT_EQ(got, 42u);
}

TEST_F(NodeTest, NamingConventions)
{
    kickmsg::Node node("mynode", "app");
    EXPECT_EQ(node.name(), "mynode");
    EXPECT_EQ(node.prefix(), "app");
}

TEST_F(NodeTest, JoinBroadcastTwoNodes)
{
    track("/test_broadcast_events");

    auto cfg = small_cfg();

    kickmsg::Node node_a("nodeA", "test");
    auto [pub_a, sub_a] = node_a.join_broadcast("events", cfg);

    kickmsg::Node node_b("nodeB", "test");
    auto [pub_b, sub_b] = node_b.join_broadcast("events", cfg);

    std::string msg_a = "hello from A";
    ASSERT_TRUE(pub_a.send(msg_a.data(), msg_a.size()));

    auto recv_b = sub_b.try_receive();
    ASSERT_TRUE(recv_b.has_value());
    EXPECT_EQ(std::string(static_cast<char const*>(recv_b->data()), recv_b->len()), msg_a);

    auto recv_a = sub_a.try_receive();
    ASSERT_TRUE(recv_a.has_value());
    EXPECT_EQ(std::string(static_cast<char const*>(recv_a->data()), recv_a->len()), msg_a);

    std::string msg_b = "reply from B";
    ASSERT_TRUE(pub_b.send(msg_b.data(), msg_b.size()));

    auto recv_a2 = sub_a.try_receive();
    ASSERT_TRUE(recv_a2.has_value());
    EXPECT_EQ(std::string(static_cast<char const*>(recv_a2->data()), recv_a2->len()), msg_b);
}

TEST_F(NodeTest, MailboxPattern)
{
    track("/test_nodeA_mbx_inbox");

    auto cfg = small_cfg();

    kickmsg::Node node_a("nodeA", "test");
    auto inbox = node_a.create_mailbox("inbox", cfg);

    kickmsg::Node node_b("nodeB", "test");
    auto reply_pub = node_b.open_mailbox("nodeA", "inbox");

    std::string reply = "version 1.0";
    ASSERT_TRUE(reply_pub.send(reply.data(), reply.size()));

    auto msg = inbox.try_receive();
    ASSERT_TRUE(msg.has_value());
    EXPECT_EQ(std::string(static_cast<char const*>(msg->data()), msg->len()), reply);
}

TEST_F(NodeTest, MailboxMultipleWriters)
{
    track("/test_owner_mbx_inbox");

    auto cfg = small_cfg();

    kickmsg::Node owner("owner", "test");
    auto inbox = owner.create_mailbox("inbox", cfg);

    kickmsg::Node writer1("w1", "test");
    auto pub1 = writer1.open_mailbox("owner", "inbox");

    kickmsg::Node writer2("w2", "test");
    auto pub2 = writer2.open_mailbox("owner", "inbox");

    std::string m1 = "from w1";
    std::string m2 = "from w2";
    ASSERT_TRUE(pub1.send(m1.data(), m1.size()));
    ASSERT_TRUE(pub2.send(m2.data(), m2.size()));

    auto r1 = inbox.try_receive();
    ASSERT_TRUE(r1.has_value());
    std::string got1(static_cast<char const*>(r1->data()), r1->len());

    auto r2 = inbox.try_receive();
    ASSERT_TRUE(r2.has_value());
    std::string got2(static_cast<char const*>(r2->data()), r2->len());

    EXPECT_TRUE((got1 == m1 && got2 == m2) || (got1 == m2 && got2 == m1));
}
