#include <gtest/gtest.h>

#include <cstring>
#include <numeric>

#include "kickcat/Mailbox.h"
#include "kickcat/EoE/mailbox/request.h"

using namespace kickcat;
using namespace kickcat::mailbox::request;

class EoE_Request : public ::testing::Test
{
public:
    void SetUp() override
    {
        std::memset(raw_message, 0, sizeof(raw_message));

        mailbox.recv_size = 256;
        mailbox.send_size = 256;

        header  = pointData<mailbox::Header>(raw_message);
        eoe     = pointData<EoE::Header>(header);
        payload = pointData<uint8_t>(eoe);

        header->address = 0;
        header->type    = mailbox::Type::EoE;
    }

protected:
    Mailbox mailbox;
    uint8_t raw_message[256];

    mailbox::Header* header;
    EoE::Header*     eoe;
    uint8_t*         payload;
};

TEST_F(EoE_Request, inactive)
{
    mailbox.recv_size = 0;
    EoE::IpParameters params{};
    ASSERT_THROW(mailbox.createEoESetIP(params), Error);
    ASSERT_THROW(mailbox.createEoEGetIP(&params), Error);
    uint8_t frame[64] = {0};
    ASSERT_THROW(mailbox.createEoESendFrame(frame, sizeof(frame)), Error);
}

TEST_F(EoE_Request, set_ip_builds_request)
{
    EoE::IpParameters params{};
    params.flags.ip_address = 1;
    params.ip[0] = 192; params.ip[1] = 168; params.ip[2] = 1; params.ip[3] = 42;

    mailbox.createEoESetIP(params);
    auto msg = mailbox.send();
    ASSERT_EQ(MessageStatus::RUNNING, msg->status());

    auto const* h = pointData<mailbox::Header>(msg->data());
    auto const* e = pointData<EoE::Header>(h);
    ASSERT_EQ(mailbox::Type::EoE, h->type);
    ASSERT_EQ(EoE::request::SET_IP, e->type);

    EoE::IpParameters parsed{};
    uint16_t len = static_cast<uint16_t>(h->len - sizeof(EoE::Header));
    ASSERT_NE(0, EoE::readIpParameters(pointData<uint8_t>(e), len, parsed));
    ASSERT_EQ(1, parsed.flags.ip_address);
    ASSERT_EQ(192, parsed.ip[0]);
    ASSERT_EQ(42,  parsed.ip[3]);
}

TEST_F(EoE_Request, set_ip_success)
{
    EoE::IpParameters params{};
    params.flags.ip_address = 1;
    mailbox.createEoESetIP(params);
    auto msg = mailbox.send();

    // ETG.1000.6 Table 84: result lives in the EoE header's last word, mailbox len = 0x04
    auto* para = pointData<EoE::ParameterHeader>(header);
    para->type   = EoE::response::SET_IP;
    para->result = EoE::result::SUCCESS;
    header->len  = sizeof(EoE::ParameterHeader);

    ASSERT_TRUE(mailbox.receive(raw_message));
    ASSERT_EQ(MessageStatus::SUCCESS, msg->status());
}

TEST_F(EoE_Request, set_ip_error_result)
{
    EoE::IpParameters params{};
    mailbox.createEoESetIP(params);
    auto msg = mailbox.send();

    auto* para = pointData<EoE::ParameterHeader>(header);
    para->type   = EoE::response::SET_IP;
    para->result = EoE::result::NO_IP_SUPPORT;
    header->len  = sizeof(EoE::ParameterHeader);

    ASSERT_TRUE(mailbox.receive(raw_message));
    ASSERT_EQ(MessageStatus::EOE_RESULT | EoE::result::NO_IP_SUPPORT, msg->status());
    ASSERT_EQ(EoE::result::NO_IP_SUPPORT, msg->status() & 0xFFFF);
}

TEST_F(EoE_Request, set_ip_rejects_truncated_response)
{
    EoE::IpParameters params{};
    mailbox.createEoESetIP(params);
    auto msg = mailbox.send();

    eoe->type   = EoE::response::SET_IP;
    header->len = sizeof(EoE::Header) - 1; // shorter than a TEOEPARAHEADER

    ASSERT_TRUE(mailbox.receive(raw_message));
    ASSERT_EQ(MessageStatus::EOE_WRONG_SERVICE, msg->status());
}

TEST_F(EoE_Request, send_frame_too_large_throws)
{
    std::vector<uint8_t> frame(EoE::MAX_FRAGMENTED_FRAME + 1);
    ASSERT_THROW(mailbox.createEoESendFrame(frame.data(), frame.size()), Error);
}

TEST_F(EoE_Request, set_ip_ignores_unrelated_message)
{
    EoE::IpParameters params{};
    mailbox.createEoESetIP(params);
    auto msg = mailbox.send();

    header->type = mailbox::Type::CoE; // not EoE -> NOOP, message stays pending
    ASSERT_FALSE(mailbox.receive(raw_message));
    ASSERT_EQ(MessageStatus::RUNNING, msg->status());
}

TEST_F(EoE_Request, get_ip_fills_parameters)
{
    EoE::IpParameters result{};
    mailbox.createEoEGetIP(&result);
    auto msg = mailbox.send();
    ASSERT_EQ(MessageStatus::RUNNING, msg->status());

    // craft a GET IP response carrying MAC + IP
    EoE::IpParameters slave{};
    slave.flags.mac_address = 1;
    slave.flags.ip_address  = 1;
    uint8_t mac[6] = {0x02, 0x00, 0x00, 0x12, 0x34, 0x56};
    std::memcpy(slave.mac, mac, sizeof(mac));
    slave.ip[0] = 10; slave.ip[1] = 0; slave.ip[2] = 0; slave.ip[3] = 7;

    eoe->type = EoE::response::GET_IP;
    size_t capacity = sizeof(raw_message) - static_cast<size_t>(payload - raw_message);
    uint16_t len = EoE::writeIpParameters(payload, capacity, slave);
    header->len  = sizeof(EoE::Header) + len;

    ASSERT_TRUE(mailbox.receive(raw_message));
    ASSERT_EQ(MessageStatus::SUCCESS, msg->status());
    ASSERT_EQ(1, result.flags.mac_address);
    ASSERT_EQ(1, result.flags.ip_address);
    ASSERT_EQ(0, std::memcmp(result.mac, mac, sizeof(mac)));
    ASSERT_EQ(10, result.ip[0]);
    ASSERT_EQ(7,  result.ip[3]);
}

TEST_F(EoE_Request, send_frame_fragments_and_reassembles)
{
    // a frame larger than the mailbox forces several fragments
    std::vector<uint8_t> frame(500);
    std::iota(frame.begin(), frame.end(), 0);

    mailbox.createEoEFrameListener();
    size_t n = mailbox.createEoESendFrame(frame.data(), frame.size(), 0);
    ASSERT_GT(n, 1u);

    // pull each queued fragment and feed it through the listener: it must rebuild the frame exactly
    for (size_t i = 0; i < n; ++i)
    {
        auto fragment = mailbox.send();
        mailbox.receive(fragment->data());
    }

    ASSERT_EQ(1u, mailbox.eoe_frames.size());
    ASSERT_EQ(frame, mailbox.eoe_frames.front());
}

TEST_F(EoE_Request, send_small_frame_single_fragment)
{
    std::vector<uint8_t> frame(40);
    std::iota(frame.begin(), frame.end(), 1);

    mailbox.createEoEFrameListener();
    size_t n = mailbox.createEoESendFrame(frame.data(), frame.size(), 0);
    ASSERT_EQ(1u, n);

    auto fragment = mailbox.send();
    auto const* e = pointData<EoE::Header>(pointData<mailbox::Header>(fragment->data()));
    ASSERT_EQ(1, e->last_fragment);
    ASSERT_EQ(0, e->fragment_number);

    mailbox.receive(fragment->data());
    ASSERT_EQ(1u, mailbox.eoe_frames.size());
    ASSERT_EQ(frame, mailbox.eoe_frames.front());
}

TEST_F(EoE_Request, eoe_frames_buffer_is_bounded)
{
    mailbox.createEoEFrameListener();

    size_t const total = mailbox::MAX_BUFFERED_MESSAGES + 5;
    for (size_t i = 0; i < total; ++i)
    {
        std::vector<uint8_t> frame(8, static_cast<uint8_t>(i)); // payload byte tags the frame
        ASSERT_EQ(1u, mailbox.createEoESendFrame(frame.data(), frame.size(), 0));
        auto fragment = mailbox.send();
        mailbox.receive(fragment->data());
    }

    ASSERT_EQ(mailbox::MAX_BUFFERED_MESSAGES, mailbox.eoe_frames.size()); // capped
    ASSERT_EQ(5, mailbox.eoe_frames.front()[0]);                          // oldest 5 dropped
    ASSERT_EQ(static_cast<uint8_t>(total - 1), mailbox.eoe_frames.back()[0]); // newest kept
}

TEST_F(EoE_Request, get_ip_null_params_is_wrong_service)
{
    mailbox.createEoEGetIP(nullptr);
    auto msg = mailbox.send();

    EoE::IpParameters slave{};
    slave.flags.ip_address = 1;
    eoe->type = EoE::response::GET_IP;
    size_t capacity = sizeof(raw_message) - static_cast<size_t>(payload - raw_message);
    uint16_t len = EoE::writeIpParameters(payload, capacity, slave);
    header->len = sizeof(EoE::Header) + len;

    ASSERT_TRUE(mailbox.receive(raw_message));
    ASSERT_EQ(MessageStatus::EOE_WRONG_SERVICE, msg->status());
}

// Pin the IP-parameter wire layout against ETG.1000.6 Table 83 (flags word then fields, by offset),
// independent of the reader — so a systematic encoder offset bug cannot hide behind a round-trip.
TEST(EoE_Protocol, ip_parameters_wire_layout)
{
    EoE::IpParameters in{};
    in.flags.mac_address = 1;
    in.flags.ip_address  = 1;
    uint8_t mac[6] = {1, 2, 3, 4, 5, 6};
    std::memcpy(in.mac, mac, sizeof(mac));
    in.ip[0] = 10; in.ip[1] = 20; in.ip[2] = 30; in.ip[3] = 40;

    uint8_t buf[64] = {0};
    uint16_t n = EoE::writeIpParameters(buf, sizeof(buf), in);
    ASSERT_EQ(4 + 6 + 4, n);
    ASSERT_EQ(0x03, buf[0]); // flags word, little-endian: mac (bit0) + ip (bit1)
    ASSERT_EQ(0x00, buf[1]);
    ASSERT_EQ(0x00, buf[2]);
    ASSERT_EQ(0x00, buf[3]);
    ASSERT_EQ(0, std::memcmp(buf + 4, mac, sizeof(mac))); // MAC at offset 4
    ASSERT_EQ(10, buf[10]);                               // IP at offset 10
    ASSERT_EQ(40, buf[13]);

    EoE::IpParameters out{};
    ASSERT_EQ(n, EoE::readIpParameters(buf, n, out));
    ASSERT_EQ(1, out.flags.mac_address);
    ASSERT_EQ(0, std::memcmp(out.mac, mac, sizeof(mac)));
    ASSERT_EQ(40, out.ip[3]);
}

// Pin the Fragmenter header fields against ETG.1000.6 Table 79/81: fragment 0 carries CompleteSize
// = ceil(size/32), later fragments carry Offset = byte_offset/32, last fragment sets LastFragment.
TEST(EoE_Protocol, fragmenter_sets_spec_headers)
{
    std::vector<uint8_t> frame(200);
    std::iota(frame.begin(), frame.end(), 0);

    EoE::Fragmenter fragmenter(frame.data(), frame.size(), 0, 5, 64); // 64-octet chunks
    EoE::Header h{};
    uint8_t const* data = nullptr;

    size_t len0 = fragmenter.next(h, data);
    ASSERT_EQ(EoE::FRAME_FRAGMENT, h.type);
    ASSERT_EQ(5, h.frame_number);
    ASSERT_EQ(0, h.fragment_number);
    ASSERT_EQ(7, h.offset);          // complete size: (200 + 31) / 32 = 7
    ASSERT_EQ(0, h.last_fragment);
    ASSERT_EQ(64u, len0);

    fragmenter.next(h, data);
    ASSERT_EQ(1, h.fragment_number);
    ASSERT_EQ(2, h.offset);          // 64 / 32
    ASSERT_EQ(0, h.last_fragment);

    fragmenter.next(h, data);
    ASSERT_EQ(2, h.fragment_number);
    ASSERT_EQ(4, h.offset);          // 128 / 32

    size_t len3 = fragmenter.next(h, data);
    ASSERT_EQ(3, h.fragment_number);
    ASSERT_EQ(1, h.last_fragment);
    ASSERT_EQ(8u, len3);             // 200 - 192
    ASSERT_TRUE(fragmenter.done());
}

TEST(EoE_Protocol, write_ip_parameters_respects_capacity)
{
    EoE::IpParameters in{};
    in.flags.dns_name = 1; // flags(4) + DNS name(32) = 36 bytes
    uint8_t buf[16] = {0};
    ASSERT_EQ(0, EoE::writeIpParameters(buf, sizeof(buf), in));
}

TEST(EoE_Protocol, parameter_accessors)
{
    EoE::IpParameters p{};
    p.flags.ip_address = 1;
    p.flags.dns_name   = 1;
    uint8_t mac[6] = {0x0a, 0x1b, 0x2c, 0x3d, 0x4e, 0x5f};
    p.ip[0] = 192; p.ip[1] = 168; p.ip[2] = 0; p.ip[3] = 254;
    std::memcpy(p.dns_name, "ethercat-slave", 14);

    ASSERT_EQ("0a:1b:2c:3d:4e:5f", EoE::macToString(mac));
    ASSERT_EQ("192.168.0.254", EoE::ipToString(p.ip));
    ASSERT_EQ("ethercat-slave", EoE::dnsName(p));
    ASSERT_NE(std::string::npos, EoE::toString(p).find("ip=192.168.0.254"));
}

TEST(EoE_Protocol, dns_name_not_nul_terminated_is_clamped)
{
    EoE::IpParameters p{};
    p.flags.dns_name = 1;
    std::memset(p.dns_name, 'a', sizeof(EoE::DNS_NAME)); // all 32 octets, no NUL terminator
    ASSERT_EQ(32u, EoE::dnsName(p).size());
    ASSERT_EQ(std::string(32, 'a'), EoE::dnsName(p));
}

TEST(EoE_Protocol, reassembler_drops_out_of_order)
{
    EoE::Reassembler r;
    EoE::Header h{};
    uint8_t data[4] = {0xAA, 0xBB, 0xCC, 0xDD};

    h.frame_number = 1; h.fragment_number = 0; h.last_fragment = 0;
    ASSERT_FALSE(r.add(&h, data, sizeof(data)));
    h.fragment_number = 2; // skips fragment 1
    ASSERT_FALSE(r.add(&h, data, sizeof(data)));

    // a fresh frame (fragment 0) restarts cleanly
    h.frame_number = 2; h.fragment_number = 0; h.last_fragment = 1;
    ASSERT_TRUE(r.add(&h, data, sizeof(data)));
    ASSERT_EQ(4u, r.frame().size());
}

TEST(EoE_Protocol, reassembler_caps_oversized_frame)
{
    EoE::Reassembler r;
    EoE::Header h{};
    std::vector<uint8_t> chunk(512, 0x7);

    h.frame_number = 0; h.last_fragment = 0;
    bool completed = false;
    for (uint16_t i = 0; i < 8; ++i) // 8 * 512 = 4096 > MAX_ETHERNET_FRAME
    {
        h.fragment_number = i;
        completed = r.add(&h, chunk.data(), chunk.size());
    }
    ASSERT_FALSE(completed);
    ASSERT_LE(r.frame().size(), EoE::MAX_ETHERNET_FRAME);
}
