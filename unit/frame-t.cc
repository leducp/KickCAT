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


TEST(Frame, read_error)
{
    Frame frame;
    std::shared_ptr<MockSocket> io_nominal{ std::make_shared<MockSocket>() };

    EXPECT_CALL(*io_nominal, read(_,_))
        .WillOnce(Return(-1))
        .WillOnce(Return(0))
        .WillOnce(Invoke([&](uint8_t* frame_in, int32_t frame_size)
        {
            EthernetHeader* header = reinterpret_cast<EthernetHeader*>(frame_in);
            header->type = 0;
            return frame_size;
        }))
        .WillOnce(Return(MAX_ETHERCAT_PAYLOAD_SIZE / 2));
    ASSERT_EQ(readFrame(io_nominal, frame), -1);
    ASSERT_EQ(readFrame(io_nominal, frame), 0);
    ASSERT_EQ(readFrame(io_nominal, frame), -1);
    ASSERT_FALSE(frame.isDatagramAvailable());

    frame.resetContext();
    frame.addDatagram(0, Command::BRD, 0, nullptr, MAX_ETHERCAT_PAYLOAD_SIZE);
    ASSERT_EQ(readFrame(io_nominal, frame), -1);
}

TEST(Frame, read_garbage)
{
    Frame frame;
    std::shared_ptr<MockSocket> io_nominal{ std::make_shared<MockSocket>() };

    EXPECT_CALL(*io_nominal, read(_,ETH_MAX_SIZE)).WillOnce(Return(ETH_MIN_SIZE));
    ASSERT_EQ(ETH_MIN_SIZE, readFrame(io_nominal, frame));
    ASSERT_TRUE(frame.isDatagramAvailable());
}


TEST(Frame, write_invalid_frame)
{
    Frame frame;
    std::shared_ptr<MockSocket> io_nominal{ std::make_shared<MockSocket>() };

    EXPECT_CALL(*io_nominal, write(_,ETH_MIN_SIZE))
    .WillOnce(Invoke([](uint8_t const* frame_in, int32_t)
    {
        {
            EthernetHeader const* header = reinterpret_cast<EthernetHeader const*>(frame_in);
            EXPECT_EQ(ETH_ETHERCAT_TYPE, header->type);
            for (int32_t i = 0; i < 6; ++i)
            {
                EXPECT_EQ(PRIMARY_IF_MAC[i], header->src[i]);
                EXPECT_EQ(0xff, header->dst[i]);
            }
        }

        {
            EthercatHeader const* header = reinterpret_cast<EthercatHeader const*>(frame_in + sizeof(EthernetHeader));
            EXPECT_EQ(header->len, 0);
        }
        return ETH_MIN_SIZE;
    }));
    ASSERT_EQ(ETH_MIN_SIZE, writeFrame(io_nominal, frame, PRIMARY_IF_MAC));
}


TEST(Frame, write_multiples_datagrams)
{
    Frame frame;
    std::shared_ptr<MockSocket> io_nominal{ std::make_shared<MockSocket>() };

    constexpr int32_t PAYLOAD_SIZE = 32;
    constexpr int32_t EXPECTED_SIZE = sizeof(EthernetHeader) + sizeof(EthercatHeader) + 3 * (sizeof(DatagramHeader) + PAYLOAD_SIZE + ETHERCAT_WKC_SIZE);
    uint8_t buffer[PAYLOAD_SIZE];
    for (int32_t i = 0; i < PAYLOAD_SIZE; ++i)
    {
        buffer[i] = static_cast<uint8_t>(i * i);
    }

    frame.addDatagram(17, Command::FPRD, 20, nullptr, PAYLOAD_SIZE);
    frame.addDatagram(18, Command::BRD,  21, nullptr, PAYLOAD_SIZE);
    frame.addDatagram(19, Command::BWR,  22, buffer,  PAYLOAD_SIZE);
    EXPECT_EQ(3, frame.datagramCounter());

    EXPECT_CALL(*io_nominal, write(_, _))
    .WillOnce(Invoke([&](uint8_t const* frame_in, int32_t frame_size)
    {
        EXPECT_EQ(EXPECTED_SIZE, frame_size);

        {
            EthernetHeader const* header = reinterpret_cast<EthernetHeader const*>(frame_in);
            EXPECT_EQ(ETH_ETHERCAT_TYPE, header->type);
            for (int32_t i = 0; i < 6; ++i)
            {
                EXPECT_EQ(PRIMARY_IF_MAC[i], header->src[i]);
                EXPECT_EQ(0xff, header->dst[i]);
            }
        }

        {
            EthercatHeader const* header = reinterpret_cast<EthercatHeader const*>(frame_in + sizeof(EthernetHeader));
            EXPECT_EQ(header->len, 3 * (sizeof(DatagramHeader) + PAYLOAD_SIZE + ETHERCAT_WKC_SIZE));
        }

        for (int32_t i = 0; i < 3; ++i)
        {
            uint8_t const* datagram = reinterpret_cast<uint8_t const*>(frame_in + sizeof(EthernetHeader) + sizeof(EthercatHeader)
                                                                        + i * (sizeof(DatagramHeader) + PAYLOAD_SIZE + ETHERCAT_WKC_SIZE));
            DatagramHeader const* header = reinterpret_cast<DatagramHeader const*>(datagram);
            uint8_t const* payload = datagram + sizeof(DatagramHeader);
            EXPECT_EQ(17 + i, header->index);
            EXPECT_EQ(20 + i, header->address);
            EXPECT_EQ(PAYLOAD_SIZE, header->len);
            EXPECT_EQ(0, header->irq);

            switch (i)
            {
                case 0:
                {
                    EXPECT_EQ(Command::FPRD, header->command);
                    EXPECT_TRUE(header->multiple);
                    for (int32_t j = 0; j < PAYLOAD_SIZE; ++j)
                    {
                        EXPECT_EQ(0, payload[j]);
                    }
                    break;
                }
                case 1:
                {
                    EXPECT_EQ(Command::BRD, header->command);
                    EXPECT_TRUE(header->multiple);
                    for (int32_t j = 0; j < PAYLOAD_SIZE; ++j)
                    {
                        EXPECT_EQ(0, payload[j]);
                    }
                    break;
                }
                case 2:
                {
                    EXPECT_EQ(Command::BWR, header->command);
                    EXPECT_FALSE(header->multiple);
                    for (int32_t j = 0; j < PAYLOAD_SIZE; ++j)
                    {
                        EXPECT_EQ(static_cast<uint8_t>(j*j), payload[j]);
                    }
                    break;
                }
                default: {}
            }
        }

        return EXPECTED_SIZE;
    }));
    ASSERT_EQ(EXPECTED_SIZE, writeFrame(io_nominal, frame, PRIMARY_IF_MAC));
}

TEST(Frame, write_error)
{
    Frame frame;
    std::shared_ptr<MockSocket> io_nominal{ std::make_shared<MockSocket>() };

    EXPECT_CALL(*io_nominal, write(_,_))
        .WillOnce(Return(-1))
        .WillOnce(Return(0));
    ASSERT_EQ(-1, writeFrame(io_nominal, frame, PRIMARY_IF_MAC));
    ASSERT_EQ(-1, writeFrame(io_nominal, frame, PRIMARY_IF_MAC));
}

