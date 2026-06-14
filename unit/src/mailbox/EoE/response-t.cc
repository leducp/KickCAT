#include <gtest/gtest.h>

#include <cstring>
#include <numeric>

#include "mocks/ESC.h"

#include "kickcat/Mailbox.h"
#include "kickcat/EoE/mailbox/request.h"
#include "kickcat/EoE/mailbox/response.h"

using namespace kickcat;
using namespace kickcat::mailbox::response;

constexpr uint16_t TEST_MAILBOX_SIZE = 256;

static std::vector<uint8_t> buildSetIP(EoE::IpParameters const& params)
{
    mailbox::request::SetIPMessage msg{TEST_MAILBOX_SIZE, params, 1ms};
    return std::vector<uint8_t>(msg.data(), msg.data() + msg.size());
}

static std::vector<uint8_t> buildGetIP()
{
    mailbox::request::GetIPMessage msg{TEST_MAILBOX_SIZE, nullptr, 1ms};
    return std::vector<uint8_t>(msg.data(), msg.data() + msg.size());
}

class EoE_Response : public ::testing::Test
{
public:
    void SetUp() override
    {
        mbx.enableEoE(store);
    }

    MockESC esc;
    EoE::IpParameters store{}; // declared before mbx so it outlives the reference
    Mailbox mbx{&esc, TEST_MAILBOX_SIZE, 4};
};

TEST_F(EoE_Response, set_ip_stores_parameters)
{
    EoE::IpParameters input{};
    input.flags.ip_address = 1;
    input.flags.subnet_mask = 1;
    input.ip[0] = 172; input.ip[1] = 16; input.ip[2] = 0; input.ip[3] = 5;
    input.subnet_mask[0] = 255; input.subnet_mask[1] = 255; input.subnet_mask[2] = 0; input.subnet_mask[3] = 0;

    auto msg = createEoEMessage(&mbx, buildSetIP(input));
    ASSERT_NE(nullptr, msg);
    ASSERT_EQ(mailbox::ProcessingResult::FINALIZE, msg->process());

    // parameters landed in the application-owned store
    ASSERT_EQ(1, store.flags.ip_address);
    ASSERT_EQ(172, store.ip[0]);
    ASSERT_EQ(5,   store.ip[3]);
    ASSERT_EQ(255, store.subnet_mask[0]);

    // reply carries a SUCCESS result in the EoE header's last word (ETG.1000.6 Table 84, N=0x04)
    auto reply = mbx.popReply();
    auto const* header = pointData<mailbox::Header>(reply.data());
    auto const* para   = pointData<EoE::ParameterHeader>(header);
    ASSERT_EQ(mailbox::Type::EoE, header->type);
    ASSERT_EQ(EoE::response::SET_IP, para->type);
    ASSERT_EQ(sizeof(EoE::ParameterHeader), header->len);
    ASSERT_EQ(EoE::result::SUCCESS, para->result);
}

TEST_F(EoE_Response, set_ip_without_support_replies_error)
{
    Mailbox bare{&esc, TEST_MAILBOX_SIZE, 4}; // EoE not enabled -> no parameter store

    EoE::IpParameters input{};
    input.flags.ip_address = 1;
    auto msg = createEoEMessage(&bare, buildSetIP(input));
    ASSERT_EQ(mailbox::ProcessingResult::FINALIZE, msg->process());

    auto reply = bare.popReply();
    auto const* para = pointData<EoE::ParameterHeader>(pointData<mailbox::Header>(reply.data()));
    ASSERT_EQ(EoE::result::NO_IP_SUPPORT, para->result);
}

TEST_F(EoE_Response, get_ip_returns_stored_parameters)
{
    store.flags.mac_address = 1;
    uint8_t mac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    std::memcpy(store.mac, mac, sizeof(mac));

    auto msg = createEoEMessage(&mbx, buildGetIP());
    ASSERT_EQ(mailbox::ProcessingResult::FINALIZE, msg->process());

    auto reply = mbx.popReply();
    auto const* header = pointData<mailbox::Header>(reply.data());
    auto const* eoe    = pointData<EoE::Header>(header);
    ASSERT_EQ(EoE::response::GET_IP, eoe->type);

    EoE::IpParameters parsed{};
    uint16_t len = static_cast<uint16_t>(header->len - sizeof(EoE::Header));
    ASSERT_NE(0, EoE::readIpParameters(pointData<uint8_t>(eoe), len, parsed));
    ASSERT_EQ(1, parsed.flags.mac_address);
    ASSERT_EQ(0, std::memcmp(parsed.mac, mac, sizeof(mac)));
}

// Master sets IP, then reads it back: the value round-trips end to end through both mailbox sides.
TEST_F(EoE_Response, round_trip_set_then_get)
{
    EoE::IpParameters input{};
    input.flags.ip_address      = 1;
    input.flags.default_gateway = 1;
    input.ip[0] = 192; input.ip[1] = 168; input.ip[2] = 10; input.ip[3] = 2;
    input.default_gateway[0] = 192; input.default_gateway[1] = 168; input.default_gateway[2] = 10; input.default_gateway[3] = 1;

    createEoEMessage(&mbx, buildSetIP(input))->process();
    (void)mbx.popReply(); // discard SET reply

    createEoEMessage(&mbx, buildGetIP())->process();
    auto get_reply = mbx.popReply();

    EoE::IpParameters parsed{};
    mailbox::request::GetIPMessage client{TEST_MAILBOX_SIZE, &parsed, 1ms};
    ASSERT_EQ(mailbox::ProcessingResult::FINALIZE, client.process(get_reply.data()));

    ASSERT_EQ(1, parsed.flags.ip_address);
    ASSERT_EQ(1, parsed.flags.default_gateway);
    ASSERT_EQ(0, std::memcmp(parsed.ip, input.ip, sizeof(EoE::IP)));
    ASSERT_EQ(0, std::memcmp(parsed.default_gateway, input.default_gateway, sizeof(EoE::IP)));
}

// A tunneled frame is fragmented by the master, reassembled by the slave, then looped back.
TEST_F(EoE_Response, frame_reassembled_and_looped_back)
{
    mbx.setEoEFrameHandler([](Mailbox& m, uint8_t const* f, size_t s, uint8_t p)
                           { m.sendEoEFrame(f, s, p); });

    std::vector<uint8_t> frame(500);
    std::iota(frame.begin(), frame.end(), 3);

    mailbox::request::Mailbox req;
    req.recv_size = TEST_MAILBOX_SIZE;
    req.send_size = TEST_MAILBOX_SIZE;
    size_t n = req.createEoESendFrame(frame.data(), frame.size(), 0);
    ASSERT_GT(n, 1u);

    // deliver each fragment as the slave would receive it: handleMessage then process()
    for (size_t i = 0; i < n; ++i)
    {
        auto fragment = req.send();
        mbx.handleMessage(std::vector<uint8_t>(fragment->data(), fragment->data() + fragment->size()));
        mbx.process();
    }

    // the slave reassembled the whole frame...
    ASSERT_EQ(1u, mbx.eoe_frames.size());
    ASSERT_EQ(frame, mbx.eoe_frames.front());

    // ...and looped it back, re-fragmenting it into the send queue
    EoE::Reassembler reassembler;
    std::vector<uint8_t> looped;
    while (true)
    {
        auto reply = mbx.popReply();
        if (reply.empty())
        {
            break;
        }
        auto const* header = pointData<mailbox::Header>(reply.data());
        auto const* eoe    = pointData<EoE::Header>(header);
        ASSERT_EQ(EoE::FRAME_FRAGMENT, eoe->type);
        size_t data_len = header->len - sizeof(EoE::Header);
        if (reassembler.add(eoe, pointData<uint8_t>(eoe), data_len))
        {
            looped = reassembler.frame();
        }
    }
    ASSERT_EQ(frame, looped);
}
