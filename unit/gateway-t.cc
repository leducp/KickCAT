#include <gtest/gtest.h>
#include "kickcat/Gateway.h"
#include "Mocks.h"

using ::testing::MockFunction;
using ::testing::Return;
using ::testing::_;
using ::testing::InSequence;

using namespace kickcat;

struct Request
{
    EthercatHeader  header;
    mailbox::Header mbx_header;
    CoE::Header coe;
    CoE::ServiceData sdo;
    int32_t data;
} __attribute__((__packed__));

class GatewayTest : public testing::Test
{
public:
    void SetUp() override
    {

    }


    MockFunction<std::shared_ptr<GatewayMessage>(uint8_t const*, int32_t, uint16_t)> mockAddMessage_;
    std::shared_ptr<MockDiagSocket> socket_{std::make_shared<MockDiagSocket>()};
    Gateway gateway{socket_, mockAddMessage_.AsStdFunction()};
};


TEST_F(GatewayTest, incoherent_request)
{
    Request req;

    // Incoherent request at gateway level
    EXPECT_CALL(*socket_, recv(_, _))
        .WillOnce(Return(std::make_tuple(-1, 0)))
        .WillOnce(Return(std::make_tuple( 1, 0)))
        .WillOnce([&](uint8_t* frame, int32_t)
        {
            req.header.type = EthercatType::ETHERCAT;
            std::memcpy(frame, &req, sizeof(req));
            return std::make_tuple<int32_t, uint16_t>(sizeof(req), 1);
        })
        .WillOnce([&](uint8_t* frame, int32_t)
        {
            req.header.type = EthercatType::MAILBOX;
            req.mbx_header.len = sizeof(req) * 2;
            std::memcpy(frame, &req, sizeof(req));
            return std::make_tuple<int32_t, uint16_t>(sizeof(req), 1);
        });
    gateway.fetchRequest();
    gateway.fetchRequest();
    gateway.fetchRequest();
    gateway.fetchRequest();

    // Message too big for the targeted mailbox
    Mailbox mbx;
    mbx.recv_size = 4;

    EXPECT_CALL(*socket_, recv(_, _))
        .WillOnce([&](uint8_t* frame, int32_t)
        {
            req.header.type = EthercatType::MAILBOX;
            req.mbx_header.len = 10;
            std::memcpy(frame, &req, sizeof(req));
            return std::make_tuple<int32_t, uint16_t>(sizeof(req), 2);
        });
    EXPECT_CALL(mockAddMessage_, Call(_, _, 2))
        .WillOnce([&](uint8_t const* raw_message, int32_t, uint16_t)
        {
            return mbx.createGatewayMessage(raw_message, mbx.recv_size + 1, 0);
        });
    gateway.fetchRequest();
}

TEST_F(GatewayTest, no_pending_request)
{
    EXPECT_CALL(*socket_, sendTo(_, _, _)).Times(0);
    gateway.processPendingRequests();
}

TEST_F(GatewayTest, nominal_loop)
{
    uint16_t GEN_GATEWAY_INDEX = 2;
    Mailbox mbx;
    mbx.recv_size = 128;

    EXPECT_CALL(*socket_, sendTo(_, 18, GEN_GATEWAY_INDEX))
    .WillOnce([](uint8_t const* frame, int32_t size, uint16_t)
    {
        Request const* req = reinterpret_cast<Request const*>(frame);
        EXPECT_EQ(1003,         req->mbx_header.address);
        EXPECT_EQ(0x1018,       req->sdo.index);
        EXPECT_EQ(0,            req->sdo.subindex);
        EXPECT_EQ(0xCAFEDECA,   req->data);
        return size;
    });

    EXPECT_CALL(*socket_, recv(_, _))
    .WillOnce([&](uint8_t* frame, int32_t)
    {
        Request req;
        req.header.type = EthercatType::MAILBOX;
        req.mbx_header.len = 10;
        req.mbx_header.address = 1003;
        req.sdo.subindex = 0;
        req.data = 0;
        std::memcpy(frame, &req, sizeof(req));
        return std::make_tuple<int32_t, uint16_t>(sizeof(req), 2);
    });
    EXPECT_CALL(mockAddMessage_, Call(_, 16, GEN_GATEWAY_INDEX))
    .WillOnce([&](uint8_t const* raw_message, int32_t raw_message_size, uint16_t gateway_index)
    {
        return mbx.createGatewayMessage(raw_message, raw_message_size, gateway_index);
    });
    gateway.fetchRequest();

    // One message is pending, not ready
    gateway.processPendingRequests();

    // Process message - not the good one
    Request answer;
    uint8_t const* raw_answer = reinterpret_cast<uint8_t const*>(&answer.mbx_header);
    answer.mbx_header.address = static_cast<uint16_t>(mailbox::GATEWAY_MESSAGE_MASK | (GEN_GATEWAY_INDEX-1));
    answer.mbx_header.len = 10;
    answer.sdo.index = 0x1018;
    answer.sdo.subindex = 0;
    answer.data = 0xCAFEDECA;
    auto msg = mbx.send();

    ASSERT_FALSE(mbx.receive(raw_answer));
    ASSERT_EQ(MessageStatus::RUNNING, msg->status());
    gateway.processPendingRequests();

    // Process message - OK
    answer.mbx_header.address = mailbox::GATEWAY_MESSAGE_MASK | GEN_GATEWAY_INDEX;
    ASSERT_TRUE(mbx.receive(raw_answer));
    ASSERT_EQ(MessageStatus::SUCCESS, msg->status());
    gateway.processPendingRequests();
}
