#include <gtest/gtest.h>
#include "kickcat/Frame.h"
#include "Mocks.h"

using ::testing::Return;
using ::testing::_;
using ::testing::Invoke;

using namespace kickcat;

TEST(Frame, default_state)
{
    Frame frame;
    frame.setSourceMAC(PRIMARY_IF_MAC);

    ASSERT_EQ(0, frame.datagramCounter());
    ASSERT_FALSE(frame.isFull());
    ASSERT_EQ(sizeof(DatagramHeader) + MAX_ETHERCAT_PAYLOAD_SIZE + ETHERCAT_WKC_SIZE, frame.freeSpace());
    ASSERT_FALSE(frame.isDatagramAvailable());
}

TEST(Frame, nextDatagram)
{
    Frame frame;
    frame.setSourceMAC(PRIMARY_IF_MAC);

    constexpr int32_t PAYLOAD_SIZE = 32;
    uint8_t buffer[PAYLOAD_SIZE];
    for (int32_t i = 0; i < PAYLOAD_SIZE; ++i)
    {
        buffer[i] = static_cast<uint8_t>(i * i);
    }

    frame.addDatagram(17, Command::FPRD, 20, nullptr, PAYLOAD_SIZE);
    frame.addDatagram(18, Command::BRD,  21, nullptr, PAYLOAD_SIZE);
    frame.addDatagram(19, Command::BWR,  22, buffer,  PAYLOAD_SIZE);
    frame.finalize();

    for (int32_t i = 0; i < 3; ++i)
    {
        auto [header, payload, wkc] = frame.nextDatagram();
        ASSERT_EQ(PAYLOAD_SIZE, header->len);
        ASSERT_EQ(17 + i, header->index);
        ASSERT_EQ(20 + i, header->address);
        ASSERT_EQ(0, wkc);

        switch (i)
        {
            case 0:
            {
                ASSERT_EQ(Command::FPRD, header->command);
                ASSERT_TRUE(header->multiple);
                for (int32_t j = 0; j < PAYLOAD_SIZE; ++j)
                {
                    ASSERT_EQ(0, payload[j]);
                }
                break;
            }
            case 1:
            {
                ASSERT_EQ(Command::BRD, header->command);
                ASSERT_TRUE(header->multiple);
                for (int32_t j = 0; j < PAYLOAD_SIZE; ++j)
                {
                    ASSERT_EQ(0, payload[j]);
                }
                break;
            }
            case 2:
            {
                ASSERT_EQ(Command::BWR, header->command);
                ASSERT_FALSE(header->multiple);
                for (int32_t j = 0; j < PAYLOAD_SIZE; ++j)
                {
                    ASSERT_EQ(static_cast<uint8_t>(j*j), payload[j]);
                }
                break;
            }
            default:
            {
                break;
            }
        }
    }
}

TEST(Frame, isFull_max_datagrams)
{
    Frame frame;
    frame.setSourceMAC(PRIMARY_IF_MAC);
    for (int32_t i = 0; i<MAX_ETHERCAT_DATAGRAMS-1; ++i)
    {
        ASSERT_EQ(i, frame.datagramCounter());
        frame.addDatagram(0, Command::BRD, 0, nullptr, 0);
        ASSERT_FALSE(frame.isFull());
    }

    frame.addDatagram(0, Command::BRD, 0, nullptr, 0);
    ASSERT_TRUE(frame.isFull());
}

TEST(Frame, isFull_big_datagram)
{
    Frame frame;
    frame.setSourceMAC(PRIMARY_IF_MAC);
    frame.addDatagram(0, Command::BRD, 0, nullptr, MAX_ETHERCAT_PAYLOAD_SIZE);
    ASSERT_TRUE(frame.isFull());
}


TEST(Frame, move_constructor)
{
    Frame frameA;
    frameA.setSourceMAC(PRIMARY_IF_MAC);
    frameA.addDatagram(0, Command::BRD, 0, nullptr, 0);
    frameA.addDatagram(0, Command::BRD, 0, nullptr, 0);

    Frame frameB = std::move(frameA);
    ASSERT_EQ(2, frameB.datagramCounter());
}


TEST(Frame, clear_frame)
{
    Frame frame;
    frame.header()->len = 1000;
    frame.clear();
    ASSERT_EQ(0, frame.header()->len);
}


TEST(Frame, finalize_minimal_size)
{
    // Create an empty frame.
    Frame frame;
    frame.setSourceMAC(PRIMARY_IF_MAC);
    ASSERT_EQ(ETH_MIN_SIZE, frame.finalize());
}

