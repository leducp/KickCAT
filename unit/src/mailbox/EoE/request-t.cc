#include <gtest/gtest.h>

#include <cstring>

#include "kickcat/EoE/mailbox/request.h"
#include "kickcat/EoE/mailbox/response.h"
#include "kickcat/Mailbox.h"

using namespace kickcat;
using namespace kickcat::mailbox::request;

namespace
{
    constexpr uint16_t MBX_SIZE = 256;

    struct TestConfig : EoE::SlaveConfig
    {
        EoE::IpParameters stored{};
        std::vector<uint8_t> last_filter;
        uint16_t set_result = EoE::result::SUCCESS;
        uint16_t get_result = EoE::result::SUCCESS;

        uint16_t setIpParameter(EoE::IpParameters const& params) override
        {
            stored = params;
            return set_result;
        }
        uint16_t getIpParameter(EoE::IpParameters& params) override
        {
            params = stored;
            return get_result;
        }
        uint16_t setAddressFilter(uint8_t const* data, size_t len) override
        {
            last_filter.assign(data, data + len);
            return EoE::result::SUCCESS;
        }
    };

    std::vector<uint8_t> frontRaw(Mailbox& master)
    {
        auto sent = master.send();
        return std::vector<uint8_t>(sent->data(), sent->data() + sent->size());
    }
}

class EoE_Request : public ::testing::Test
{
public:
    void SetUp() override
    {
        master.recv_size = MBX_SIZE;
        master.send_size = MBX_SIZE;
        slave.enableEoE(config, nullptr);
    }

protected:
    Mailbox master;
    mailbox::response::Mailbox slave{MBX_SIZE, 4};
    TestConfig config;
};

TEST_F(EoE_Request, set_ip_parameter)
{
    EoE::IpParameters params{};
    params.ip_included = true;
    params.ip[0] = 192; params.ip[1] = 168; params.ip[2] = 1; params.ip[3] = 42;
    params.mac_included = true;
    uint8_t mac[6] = {1, 2, 3, 4, 5, 6};
    std::memcpy(params.mac, mac, sizeof(mac));

    auto msg = master.createEoESetIP(params, 1s);
    auto reply = slave.processRequest(frontRaw(master));
    ASSERT_FALSE(reply.empty());

    ASSERT_TRUE(master.receive(reply.data()));
    ASSERT_EQ(MessageStatus::SUCCESS, msg->status());
    ASSERT_TRUE(config.stored.ip_included);
    ASSERT_EQ(42, config.stored.ip[3]);
    ASSERT_TRUE(config.stored.mac_included);
    ASSERT_EQ(6, config.stored.mac[5]);
}

TEST_F(EoE_Request, set_ip_parameter_error_mapping)
{
    config.set_result = EoE::result::NO_IP_SUPPORT;

    EoE::IpParameters params{};
    params.ip_included = true;
    auto msg = master.createEoESetIP(params, 1s);
    auto reply = slave.processRequest(frontRaw(master));
    ASSERT_FALSE(reply.empty());

    ASSERT_TRUE(master.receive(reply.data()));
    ASSERT_EQ(MessageStatus::EOE_NO_IP_SUPPORT, msg->status());
}

TEST_F(EoE_Request, get_ip_parameter)
{
    config.stored.ip_included = true;
    config.stored.ip[0] = 10; config.stored.ip[3] = 7;
    config.stored.subnet_included = true;
    config.stored.subnet[0] = 255; config.stored.subnet[1] = 255; config.stored.subnet[2] = 255;

    EoE::IpParameters out{};
    auto msg = master.createEoEGetIP(&out, 1s);
    auto reply = slave.processRequest(frontRaw(master));
    ASSERT_FALSE(reply.empty());

    ASSERT_TRUE(master.receive(reply.data()));
    ASSERT_EQ(MessageStatus::SUCCESS, msg->status());
    ASSERT_TRUE(out.ip_included);
    ASSERT_EQ(7, out.ip[3]);
    ASSERT_TRUE(out.subnet_included);
    ASSERT_EQ(255, out.subnet[2]);
    ASSERT_EQ(0, out.subnet[3]);
}

TEST_F(EoE_Request, set_address_filter)
{
    EoE::AddressFilter filter;
    filter.inhibit_broadcast = true;
    filter.addresses.push_back({0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF});
    filter.addresses.push_back({0x11, 0x22, 0x33, 0x44, 0x55, 0x66});

    auto msg = master.createEoESetAddressFilter(filter, 1s);
    auto reply = slave.processRequest(frontRaw(master));
    ASSERT_FALSE(reply.empty());

    ASSERT_TRUE(master.receive(reply.data()));
    ASSERT_EQ(MessageStatus::SUCCESS, msg->status());
    // AddressFilterHeader (2 bytes) + two MAC addresses (12 bytes)
    ASSERT_EQ(2u + 12u, config.last_filter.size());
}

TEST_F(EoE_Request, get_ip_oversized_response_is_rejected)
{
    EoE::IpParameters out{};
    auto msg = master.createEoEGetIP(&out, 1s);
    auto sent = master.send();

    std::vector<uint8_t> reply(MBX_SIZE, 0);
    auto* header = pointData<mailbox::Header>(reply.data());
    auto* eoe    = pointData<EoE::Header>(header);
    header->type  = mailbox::Type::EoE;
    header->count = pointData<mailbox::Header>(sent->data())->count;
    header->len   = 0xFFFF;   // claims far more than the buffer holds
    eoe->type     = EoE::frame_type::GET_IP_RESP;

    ASSERT_TRUE(master.receive(reply.data()));
    ASSERT_EQ(MessageStatus::EOE_WRONG_SERVICE, msg->status());
}

TEST_F(EoE_Request, frame_master_to_slave_single_fragment)
{
    std::vector<uint8_t> received;
    uint8_t received_port = 0xFF;
    mailbox::response::Mailbox sink_slave{128, 4};
    TestConfig sink_config;
    sink_slave.enableEoE(sink_config, [&](uint8_t const* f, size_t n, uint8_t port)
    {
        received.assign(f, f + n);
        received_port = port;
    });

    Mailbox tx;
    tx.recv_size = 128;
    tx.send_size = 128;

    std::vector<uint8_t> frame(40);
    for (size_t i = 0; i < frame.size(); ++i) { frame[i] = static_cast<uint8_t>(i); }
    tx.sendEoEFrame(frame.data(), frame.size(), 3);

    while (not tx.to_send.empty())
    {
        auto sent = tx.send();
        sink_slave.handleMessage(std::vector<uint8_t>(sent->data(), sent->data() + sent->size()));
        sink_slave.process();
    }

    ASSERT_EQ(frame, received);
    ASSERT_EQ(3, received_port);
}

TEST_F(EoE_Request, frame_master_to_slave_multi_fragment)
{
    std::vector<uint8_t> received;
    mailbox::response::Mailbox sink_slave{128, 8};
    TestConfig sink_config;
    sink_slave.enableEoE(sink_config, [&](uint8_t const* f, size_t n, uint8_t)
    {
        received.assign(f, f + n);
    });

    Mailbox tx;
    tx.recv_size = 128;
    tx.send_size = 128;

    std::vector<uint8_t> frame(1500);
    for (size_t i = 0; i < frame.size(); ++i) { frame[i] = static_cast<uint8_t>(i & 0xFF); }
    tx.sendEoEFrame(frame.data(), frame.size(), 0);

    while (not tx.to_send.empty())
    {
        auto sent = tx.send();
        sink_slave.handleMessage(std::vector<uint8_t>(sent->data(), sent->data() + sent->size()));
        sink_slave.process();
    }

    ASSERT_EQ(frame, received);
}
