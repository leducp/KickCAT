#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "kickcat/Mailbox.h"

using namespace kickcat;
using namespace kickcat::mailbox::request;

class Mailbox_Request : public ::testing::Test
{
public:
    void SetUp() override
    {
        mailbox.recv_size = 256;
        mailbox.send_size = 256;
    }

protected:
    Mailbox mailbox;
    uint8_t raw_message[256]{};
};

TEST_F(Mailbox_Request, SyncManager_configuration)
{
    mailbox.send_size = 17;
    mailbox.send_offset = 8;
    mailbox.recv_size = 42;
    mailbox.recv_offset = 0x300;

    SyncManager::Register SM[2];
    mailbox.generateSMConfig(SM);

    ASSERT_EQ(42,       SM[0].length);
    ASSERT_EQ(0x300,    SM[0].start_address);
    ASSERT_EQ(1,        SM[0].activate);
    ASSERT_EQ(0x26,     SM[0].control);

    ASSERT_EQ(17,       SM[1].length);
    ASSERT_EQ(8,        SM[1].start_address);
    ASSERT_EQ(1,        SM[1].activate);
    ASSERT_EQ(0x22,     SM[1].control);
}

TEST_F(Mailbox_Request, counter)
{
    for (int i = mailbox.nextCounter(); i < 100; ++i)
    {
        ASSERT_EQ(i % 7 + 1, mailbox.nextCounter());
    }
}

TEST_F(Mailbox_Request, received_unknown_message)
{
    ASSERT_FALSE(mailbox.receive(raw_message));
}


// The reply is read from the send mailbox, which an asymmetric SII can declare larger than the
// receive mailbox the request buffer was sized from.
TEST_F(Mailbox_Request, gateway_reply_larger_than_the_request_buffer)
{
    mailbox.recv_size = 32;
    mailbox.send_size = 256;

    std::vector<uint8_t> request(mailbox.recv_size, 0);
    auto* request_header = pointData<mailbox::Header>(request.data());
    request_header->len     = 10;
    request_header->address = 0x1001;
    request_header->type    = mailbox::Type::CoE;

    auto msg = mailbox.createGatewayMessage(request.data(), static_cast<int32_t>(request.size()), 1);
    ASSERT_NE(nullptr, msg);
    ASSERT_EQ(msg, mailbox.send());

    std::vector<uint8_t> reply(mailbox.send_size, 0);
    auto* reply_header = pointData<mailbox::Header>(reply.data());
    reply_header->len     = 210;   // 216 bytes: fits the send mailbox, not the request buffer
    reply_header->address = mailbox::GATEWAY_MESSAGE_MASK | 1;
    reply_header->type    = mailbox::Type::CoE;

    ASSERT_TRUE(mailbox.receive(reply.data()));
    EXPECT_EQ(MessageStatus::SUCCESS, msg->status());
    EXPECT_EQ(216u, msg->size());

    // Storing the reply grows the buffer, so the cached header pointer must follow it: the address
    // restored below lands in the reply the client receives.
    EXPECT_EQ(0x1001, pointData<mailbox::Header>(msg->data())->address);
}


TEST_F(Mailbox_Request, gateway_reply_longer_than_the_send_mailbox_is_dropped)
{
    mailbox.recv_size = 256;
    mailbox.send_size = 32;

    std::vector<uint8_t> request(64, 0);
    auto* request_header = pointData<mailbox::Header>(request.data());
    request_header->len     = 10;
    request_header->address = 0x1001;
    request_header->type    = mailbox::Type::CoE;

    auto msg = mailbox.createGatewayMessage(request.data(), static_cast<int32_t>(request.size()), 2);
    ASSERT_NE(nullptr, msg);
    ASSERT_EQ(msg, mailbox.send());

    // Only send_size bytes were fetched from the slave, so a longer claim cannot be read.
    std::vector<uint8_t> reply(mailbox.send_size, 0);
    auto* reply_header = pointData<mailbox::Header>(reply.data());
    reply_header->len     = 100;
    reply_header->address = mailbox::GATEWAY_MESSAGE_MASK | 2;
    reply_header->type    = mailbox::Type::CoE;

    EXPECT_FALSE(mailbox.receive(reply.data()));
    EXPECT_EQ(MessageStatus::RUNNING, msg->status());
}
