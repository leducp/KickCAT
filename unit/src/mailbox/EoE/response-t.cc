#include <gtest/gtest.h>

#include <cstring>

#include "kickcat/EoE/mailbox/request.h"
#include "kickcat/EoE/mailbox/response.h"
#include "kickcat/Mailbox.h"

using namespace kickcat;

namespace
{
    constexpr uint16_t MBX_SIZE = 256;

    struct TestConfig : EoE::SlaveConfig
    {
        EoE::IpParameters stored{};
        uint16_t setIpParameter(EoE::IpParameters const& params) override { stored = params; return EoE::result::SUCCESS; }
        uint16_t getIpParameter(EoE::IpParameters& params) override { params = stored; return EoE::result::SUCCESS; }
        uint16_t setAddressFilter(uint8_t const*, size_t) override { return EoE::result::SUCCESS; }
    };

    std::vector<uint8_t> buildSetIP()
    {
        EoE::IpParameters params{};
        params.ip_included = true;
        params.ip[3] = 9;
        mailbox::request::SetIPParameterMessage msg{MBX_SIZE, params, 1s};
        return std::vector<uint8_t>(msg.data(), msg.data() + msg.size());
    }
}

TEST(EoE_Response, set_ip_without_config_reports_no_support)
{
    mailbox::response::Mailbox slave{MBX_SIZE, 2};   // enableEoE NOT called
    // The factory is only registered by enableEoE, so an EoE message hits no handler.
    auto reply = slave.processRequest(buildSetIP());
    ASSERT_FALSE(reply.empty());
    auto const* header = pointData<mailbox::Header>(reply.data());
    ASSERT_EQ(mailbox::ERR, header->type);
}

TEST(EoE_Response, set_ip_handled_and_acked)
{
    mailbox::response::Mailbox slave{MBX_SIZE, 2};
    TestConfig config;
    slave.enableEoE(config, nullptr);

    auto reply = slave.processRequest(buildSetIP());
    ASSERT_FALSE(reply.empty());

    auto const* header = pointData<mailbox::Header>(reply.data());
    ASSERT_EQ(mailbox::Type::EoE, header->type);
    auto const* resp = pointData<EoE::ParameterResponse>(header);
    ASSERT_EQ(EoE::frame_type::SET_IP_RESP, resp->type);
    ASSERT_EQ(EoE::result::SUCCESS, resp->result);
    ASSERT_EQ(9, config.stored.ip[3]);
}

TEST(EoE_Response, unknown_frame_type_replies_error)
{
    mailbox::response::Mailbox slave{MBX_SIZE, 2};
    TestConfig config;
    slave.enableEoE(config, nullptr);

    std::vector<uint8_t> raw(MBX_SIZE, 0);
    auto* header = pointData<mailbox::Header>(raw.data());
    auto* eoe    = pointData<EoE::Header>(header);
    header->type = mailbox::Type::EoE;
    header->len  = sizeof(EoE::Header);
    eoe->type    = 0xD;   // not a defined EoE frame type

    auto reply = slave.processRequest(std::move(raw));
    ASSERT_FALSE(reply.empty());
    auto const* reply_header = pointData<mailbox::Header>(reply.data());
    ASSERT_EQ(mailbox::ERR, reply_header->type);
}

TEST(EoE_Response, oversized_length_is_rejected)
{
    mailbox::response::Mailbox slave{MBX_SIZE, 2};
    TestConfig config;
    slave.enableEoE(config, nullptr);

    std::vector<uint8_t> raw(MBX_SIZE, 0);
    auto* header = pointData<mailbox::Header>(raw.data());
    auto* eoe    = pointData<EoE::Header>(header);
    header->type = mailbox::Type::EoE;
    header->len  = 0xFFFF;   // beyond the buffer; must be rejected before parsing the payload
    eoe->type    = EoE::frame_type::SET_IP_REQ;

    auto reply = slave.processRequest(std::move(raw));
    ASSERT_FALSE(reply.empty());
    auto const* reply_header = pointData<mailbox::Header>(reply.data());
    ASSERT_EQ(mailbox::ERR, reply_header->type);
}

TEST(EoE_Response, frame_slave_to_master)
{
    mailbox::response::Mailbox slave{128, 8};

    mailbox::request::Mailbox master;
    master.recv_size = 128;
    master.send_size = 128;

    std::vector<uint8_t> received;
    uint8_t received_port = 0xFF;
    auto receiver = std::make_shared<mailbox::request::EoEReceiveMessage>(master.recv_size);
    receiver->setFrameSink([&](uint8_t const* f, size_t n, uint8_t port)
    {
        received.assign(f, f + n);
        received_port = port;
    });
    master.to_process.push_back(receiver);

    std::vector<uint8_t> frame(1500);
    for (size_t i = 0; i < frame.size(); ++i) { frame[i] = static_cast<uint8_t>((i * 3) & 0xFF); }
    slave.sendEoEFrame(frame.data(), frame.size(), 2);

    while (true)
    {
        auto raw = slave.popReply();
        if (raw.empty()) { break; }
        master.receive(raw.data());
    }

    ASSERT_EQ(frame, received);
    ASSERT_EQ(2, received_port);
}
