#include <gtest/gtest.h>

#include "Mocks.h"
#include "kickcat/SocketNull.h"
#include "kickcat/DebugHelpers.h"

using namespace kickcat;

TEST(DebugHelpers, sendGetRegister)
{
    std::shared_ptr<MockSocket> io_nominal{ std::make_shared<MockSocket>() };
    std::shared_ptr<SocketNull> io_redundancy{ std::make_shared<SocketNull>() };
    std::shared_ptr<Link> link = std::make_shared<Link>(io_nominal, io_redundancy, nullptr);
    std::vector<DatagramCheck<uint8_t>> expected(1, {Command::FPRD, 0, false});

    //Succesful read
    uint16_t value_read = 1;
    io_nominal->checkSendFrame(expected);
    EXPECT_CALL(*io_nominal, setTimeout(testing::_));
    io_nominal->handleReply<uint16_t>({0xFF}, 1);
    sendGetRegister(*link, 0x00, 0x110, value_read);
    ASSERT_EQ(value_read, 0xFF);

    //Invalid WKC
    io_nominal->checkSendFrame(expected);
    EXPECT_CALL(*io_nominal, setTimeout(testing::_));
    io_nominal->handleReply<uint16_t>({0xFF}, 2);
    ASSERT_THROW(sendGetRegister(*link, 0x00, 0x110, value_read), Error);
}

TEST(DebugHelpers, sendWriteRegister)
{
    std::shared_ptr<MockSocket> io_nominal{ std::make_shared<MockSocket>() };
    std::shared_ptr<SocketNull> io_redundancy{ std::make_shared<SocketNull>() };
    std::shared_ptr<Link> link = std::make_shared<Link>(io_nominal, io_redundancy, nullptr);
    std::vector<DatagramCheck<uint8_t>> expected(1, {Command::FPWR, 0, false});

    // Succesful write
    uint16_t value_write = 0x00;
    io_nominal->checkSendFrame(expected);
    EXPECT_CALL(*io_nominal, setTimeout(testing::_));
    io_nominal->handleReply<uint8_t>({0}, 1);
    sendWriteRegister(*link, 0x00, 0x110, value_write);
    ASSERT_EQ(value_write, 0);

    //Invalid WKC
    io_nominal->checkSendFrame(expected);
    EXPECT_CALL(*io_nominal, setTimeout(testing::_));
    io_nominal->handleReply<uint16_t>({0xFF}, 2);
    ASSERT_THROW(sendWriteRegister(*link, 0x00, 0x110, value_write), Error);
}
