#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "mocks/Link.h"
#include "mocks/Sockets.h"

#include "kickcat/Bus.h"
#include "kickcat/Gateway.h"
#include "kickcat/Mailbox.h"
#include "kickcat/MasterOD.h"
#include "kickcat/CoE/mailbox/request.h"

using namespace kickcat;
using ::testing::_;

constexpr uint16_t MBX_SIZE = 256;

static MasterIdentity testIdentity()
{
    MasterIdentity id;
    id.device_type   = 0x12345678;
    id.vendor_id     = 0xAABBCCDD;
    id.product_code  = 0x11223344;
    id.revision      = 0x00000001;
    id.serial_number = 0xDEADBEEF;
    return id;
}


class MasterGatewayTest : public testing::Test
{
public:
    void SetUp() override
    {
        MasterOD od(testIdentity());
        mbx.enableCoE(od.createDictionary());
        bus.setMasterMailbox(&mbx);
    }

    std::shared_ptr<MockLink> link{std::make_shared<MockLink>()};
    mailbox::response::Mailbox mbx{MBX_SIZE};
    Bus bus{link};
};


TEST_F(MasterGatewayTest, address_zero_reads_master_identity)
{
    // SDO upload 0x1018:01 (Vendor ID) with the default mailbox header address == 0 targets the master OD.
    uint32_t data{0};
    uint32_t data_size = sizeof(data);
    mailbox::request::SDOMessage sdo_msg{MBX_SIZE, 0x1018, 1, false, CoE::SDO::request::UPLOAD, &data, &data_size, 1ms};
    ASSERT_EQ(0u, sdo_msg.address());

    auto gw_msg = bus.addGatewayMessage(sdo_msg.data(), static_cast<int32_t>(sdo_msg.size()), 42);
    ASSERT_NE(nullptr, gw_msg);
    EXPECT_EQ(42u, gw_msg->gatewayIndex());
    EXPECT_EQ(mailbox::request::MessageStatus::SUCCESS, gw_msg->status());

    auto const* reply_header = pointData<mailbox::Header>(gw_msg->data());
    auto const* coe          = pointData<CoE::Header>(reply_header);
    auto const* sdo          = pointData<CoE::ServiceData>(coe);
    auto const* payload      = pointData<uint32_t>(sdo);

    EXPECT_EQ(mailbox::Type::CoE,         reply_header->type);
    EXPECT_EQ(0u,                         reply_header->address); // SDO response preserves the request's address field
    EXPECT_EQ(CoE::Service::SDO_RESPONSE, coe->service);
    EXPECT_EQ(CoE::SDO::response::UPLOAD, sdo->command);
    EXPECT_EQ(testIdentity().vendor_id,   *payload);
}


TEST_F(MasterGatewayTest, malformed_tiny_request_returns_nullptr)
{
    // Regression: the size guard in Bus::addGatewayMessage must reject requests smaller than a
    // mailbox header without dereferencing them (no OOB read).
    uint8_t tiny[3] = {0};
    EXPECT_EQ(nullptr, bus.addGatewayMessage(tiny, sizeof(tiny), 1));
    EXPECT_EQ(nullptr, bus.addGatewayMessage(tiny, 0, 1));
    EXPECT_EQ(nullptr, bus.addGatewayMessage(tiny, -1, 1));
}


TEST_F(MasterGatewayTest, unknown_slave_address_still_returns_nullptr_when_master_mailbox_set)
{
    // Regression: installing a master mailbox must not reroute slave-addressed requests to it.
    // Unknown slave addresses continue to fall through to the "no slave on the bus" error path.
    uint32_t data{0};
    uint32_t data_size = sizeof(data);
    mailbox::request::SDOMessage sdo_msg{MBX_SIZE, 0x1018, 1, false, CoE::SDO::request::UPLOAD, &data, &data_size, 1ms};
    sdo_msg.setAddress(0x1234);

    EXPECT_EQ(nullptr, bus.addGatewayMessage(sdo_msg.data(), static_cast<int32_t>(sdo_msg.size()), 1));
}


TEST_F(MasterGatewayTest, address_zero_unknown_object_returns_sdo_abort)
{
    // Unknown object must come back as an SDO abort, not as nullptr: the master mailbox owns
    // the error reply and the gateway path must deliver it to the client.
    uint32_t data{0};
    uint32_t data_size = sizeof(data);
    mailbox::request::SDOMessage sdo_msg{MBX_SIZE, 0x9999, 0, false, CoE::SDO::request::UPLOAD, &data, &data_size, 1ms};

    auto gw_msg = bus.addGatewayMessage(sdo_msg.data(), static_cast<int32_t>(sdo_msg.size()), 7);
    ASSERT_NE(nullptr, gw_msg);
    EXPECT_EQ(mailbox::request::MessageStatus::SUCCESS, gw_msg->status());

    auto const* coe = pointData<CoE::Header>(pointData<mailbox::Header>(gw_msg->data()));
    auto const* sdo = pointData<CoE::ServiceData>(coe);
    auto const* payload = pointData<uint32_t>(sdo);

    EXPECT_EQ(CoE::Service::SDO_REQUEST,                 coe->service);
    EXPECT_EQ(CoE::SDO::request::ABORT,                  sdo->command);
    EXPECT_EQ(CoE::SDO::abort::OBJECT_DOES_NOT_EXIST,    *payload);
}


TEST(MasterGatewayNoMailbox, address_zero_without_master_mailbox_returns_nullptr)
{
    // Regression: with no master mailbox registered, the legacy "not implemented" error path is kept.
    auto link = std::make_shared<MockLink>();
    Bus bus{link};

    uint32_t data{0};
    uint32_t data_size = sizeof(data);
    mailbox::request::SDOMessage sdo_msg{MBX_SIZE, 0x1018, 1, false, CoE::SDO::request::UPLOAD, &data, &data_size, 1ms};

    EXPECT_EQ(nullptr, bus.addGatewayMessage(sdo_msg.data(), static_cast<int32_t>(sdo_msg.size()), 1));
}


TEST(MasterGatewayNoMailbox, unknown_slave_address_still_returns_nullptr)
{
    // Regression: unknown slave addresses return nullptr regardless of master mailbox state.
    auto link = std::make_shared<MockLink>();
    Bus bus{link};

    uint32_t data{0};
    uint32_t data_size = sizeof(data);
    mailbox::request::SDOMessage sdo_msg{MBX_SIZE, 0x1018, 1, false, CoE::SDO::request::UPLOAD, &data, &data_size, 1ms};
    sdo_msg.setAddress(0x1234);

    EXPECT_EQ(nullptr, bus.addGatewayMessage(sdo_msg.data(), static_cast<int32_t>(sdo_msg.size()), 1));
}


TEST_F(MasterGatewayTest, full_udp_loop_through_gateway)
{
    // End-to-end: fake a UDP client sending an ETG.8200 frame targeting the master OD (address=0),
    // run it through Gateway::fetchRequest() + Bus::addGatewayMessage() + Gateway::processPendingRequests(),
    // and verify the reply comes back out of the same socket.
    auto socket = std::make_shared<MockDiagSocket>();
    using namespace std::placeholders;
    Gateway gateway{socket, std::bind(&Bus::addGatewayMessage, &bus, _1, _2, _3)};

    constexpr uint16_t GATEWAY_INDEX = 5;

    uint32_t data{0};
    uint32_t data_size = sizeof(data);
    mailbox::request::SDOMessage sdo_msg{MBX_SIZE, 0x1018, 2, false, CoE::SDO::request::UPLOAD, &data, &data_size, 1ms};

    std::vector<uint8_t> udp_frame(sizeof(EthercatHeader) + sdo_msg.size());
    auto* eth_header = reinterpret_cast<EthercatHeader*>(udp_frame.data());
    eth_header->type = EthercatType::MAILBOX;
    eth_header->len  = sdo_msg.size() & 0x7ff;
    std::memcpy(udp_frame.data() + sizeof(EthercatHeader), sdo_msg.data(), sdo_msg.size());

    EXPECT_CALL(*socket, recv(_, _))
        .WillOnce([&](void* frame, int32_t frame_size) -> std::tuple<int32_t, uint16_t>
        {
            EXPECT_GE(frame_size, static_cast<int32_t>(udp_frame.size()));
            std::memcpy(frame, udp_frame.data(), udp_frame.size());
            return {static_cast<int32_t>(udp_frame.size()), GATEWAY_INDEX};
        });

    EXPECT_CALL(*socket, sendTo(_, _, GATEWAY_INDEX))
        .WillOnce([&](void const* frame, int32_t size, uint16_t) -> int32_t
        {
            auto const* hdr = reinterpret_cast<EthercatHeader const*>(frame);
            EXPECT_EQ(EthercatType::MAILBOX, hdr->type);

            auto const* mbx_hdr = reinterpret_cast<mailbox::Header const*>(
                static_cast<uint8_t const*>(frame) + sizeof(EthercatHeader));
            auto const* coe     = pointData<CoE::Header>(mbx_hdr);
            auto const* sdo     = pointData<CoE::ServiceData>(coe);
            auto const* payload = pointData<uint32_t>(sdo);

            EXPECT_EQ(0u,                            mbx_hdr->address);
            EXPECT_EQ(CoE::Service::SDO_RESPONSE,    coe->service);
            EXPECT_EQ(CoE::SDO::response::UPLOAD,    sdo->command);
            EXPECT_EQ(testIdentity().product_code,   *payload);
            return size;
        });

    gateway.fetchRequest();
    gateway.processPendingRequests();
}
