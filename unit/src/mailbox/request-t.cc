#include <gtest/gtest.h>

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

    SyncManager SM[2];
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
