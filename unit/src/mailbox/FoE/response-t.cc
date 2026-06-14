#include <gtest/gtest.h>

#include <cstring>
#include <numeric>

#include "mocks/ESC.h"

#include "kickcat/Mailbox.h"
#include "kickcat/FoE/mailbox/request.h"
#include "kickcat/FoE/mailbox/response.h"

using namespace kickcat;

// Small mailbox to force multi-packet transfers: max FoE Data payload = 32 - 12 = 20 octets.
constexpr uint16_t TEST_MAILBOX_SIZE = 32;

// Run a master-side transfer to completion against a slave-side mailbox: each cycle the master
// emits one message, the slave processes it and the master consumes every reply (which may queue
// the next master message). Mirrors the real receive/process/send cadence.
static void drive(mailbox::request::Mailbox& req, mailbox::response::Mailbox& resp)
{
    for (int guard = 0; (guard < 100000) and (not req.to_send.empty()); ++guard)
    {
        auto out = req.send();
        resp.handleMessage(std::vector<uint8_t>(out->data(), out->data() + out->size()));
        resp.process();
        for (auto reply = resp.popReply(); not reply.empty(); reply = resp.popReply())
        {
            req.receive(reply.data());
        }
    }
}

class FoE_Response : public ::testing::Test
{
public:
    void SetUp() override
    {
        resp.enableFoE(fs);
        req.recv_size = TEST_MAILBOX_SIZE;
        req.send_size = TEST_MAILBOX_SIZE;
    }

    MockESC esc;
    mailbox::response::InMemoryFileSystem fs;                 // declared before resp: outlives it
    mailbox::response::Mailbox resp{&esc, TEST_MAILBOX_SIZE, 4};
    mailbox::request::Mailbox  req;
};

TEST_F(FoE_Response, read_round_trip_multi_packet)
{
    std::vector<uint8_t> content(50);
    std::iota(content.begin(), content.end(), 0);
    fs.setFile("fw.bin", content);

    std::vector<uint8_t> out(128, 0);
    uint32_t size = static_cast<uint32_t>(out.size());
    auto msg = req.createFoERead("fw.bin", 0, out.data(), &size);

    drive(req, resp);

    ASSERT_EQ(mailbox::request::MessageStatus::SUCCESS, msg->status());
    ASSERT_EQ(content.size(), size);
    out.resize(size);
    ASSERT_EQ(content, out);
}

TEST_F(FoE_Response, read_exact_multiple_of_packet_size)
{
    // 40 = 2 * max payload (20): the slave must emit a terminating zero-length Data packet
    std::vector<uint8_t> content(40);
    std::iota(content.begin(), content.end(), 7);
    fs.setFile("exact.bin", content);

    std::vector<uint8_t> out(128, 0xFF);
    uint32_t size = static_cast<uint32_t>(out.size());
    auto msg = req.createFoERead("exact.bin", 0, out.data(), &size);

    drive(req, resp);

    ASSERT_EQ(mailbox::request::MessageStatus::SUCCESS, msg->status());
    ASSERT_EQ(content.size(), size);
    out.resize(size);
    ASSERT_EQ(content, out);
}

TEST_F(FoE_Response, read_not_found)
{
    std::vector<uint8_t> out(32, 0);
    uint32_t size = static_cast<uint32_t>(out.size());
    auto msg = req.createFoERead("missing", 0, out.data(), &size);

    drive(req, resp);

    ASSERT_EQ(mailbox::request::MessageStatus::FOE_RESULT | FoE::result::NOT_FOUND, msg->status());
}

TEST_F(FoE_Response, read_empty_file)
{
    fs.setFile("empty", {});
    std::vector<uint8_t> out(32, 0xFF);
    uint32_t size = static_cast<uint32_t>(out.size());
    auto msg = req.createFoERead("empty", 0, out.data(), &size);

    drive(req, resp);

    ASSERT_EQ(mailbox::request::MessageStatus::SUCCESS, msg->status());
    ASSERT_EQ(0u, size);
}

TEST_F(FoE_Response, write_round_trip_multi_packet)
{
    std::vector<uint8_t> content(50);
    std::iota(content.begin(), content.end(), 100);

    auto msg = req.createFoEWrite("up.bin", 0, content.data(), static_cast<uint32_t>(content.size()));

    drive(req, resp);

    ASSERT_EQ(mailbox::request::MessageStatus::SUCCESS, msg->status());
    ASSERT_TRUE(fs.hasFile("up.bin"));
    ASSERT_EQ(content, fs.file("up.bin"));
}

TEST_F(FoE_Response, write_exact_multiple_of_packet_size)
{
    // 40 = 2 * max payload (20): forces a terminating zero-length Data packet
    std::vector<uint8_t> content(40);
    std::iota(content.begin(), content.end(), 1);

    auto msg = req.createFoEWrite("aligned.bin", 0, content.data(), static_cast<uint32_t>(content.size()));

    drive(req, resp);

    ASSERT_EQ(mailbox::request::MessageStatus::SUCCESS, msg->status());
    ASSERT_EQ(content, fs.file("aligned.bin"));
}

TEST_F(FoE_Response, write_empty_file)
{
    auto msg = req.createFoEWrite("empty.bin", 0, nullptr, 0);

    drive(req, resp);

    ASSERT_EQ(mailbox::request::MessageStatus::SUCCESS, msg->status());
    ASSERT_TRUE(fs.hasFile("empty.bin"));
    ASSERT_TRUE(fs.file("empty.bin").empty());
}

namespace
{
    class FailingWriteFs final : public mailbox::response::AbstractFileSystem
    {
    public:
        uint32_t read(std::string const&, uint32_t, std::vector<uint8_t>&) override { return 0; }
        uint32_t write(std::string const&, uint32_t, std::vector<uint8_t> const&) override
        {
            return FoE::result::DISK_FULL;
        }
    };
}

TEST_F(FoE_Response, write_commit_failure_surfaces_foe_error)
{
    FailingWriteFs bad_fs;
    mailbox::response::Mailbox bad_resp{&esc, TEST_MAILBOX_SIZE, 4};
    bad_resp.enableFoE(bad_fs);

    std::vector<uint8_t> content(10);
    std::iota(content.begin(), content.end(), 1);
    auto msg = req.createFoEWrite("fail.bin", 0, content.data(), static_cast<uint32_t>(content.size()));

    drive(req, bad_resp);

    ASSERT_EQ(mailbox::request::MessageStatus::FOE_RESULT | FoE::result::DISK_FULL, msg->status());
}
