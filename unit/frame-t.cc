#include <gtest/gtest.h>
#include "Frame.h"
#include "Mocks.h"

using ::testing::Return;
using ::testing::_;
using ::testing::Invoke;

using namespace kickcat;

TEST(Frame, default_state)
{
    Frame frame{PRIMARY_IF_MAC};

    ASSERT_EQ(0, frame.datagramCounter());
    ASSERT_FALSE(frame.isFull());
    ASSERT_EQ(sizeof(DatagramHeader) + MAX_ETHERCAT_PAYLOAD_SIZE + ETHERCAT_WKC_SIZE, frame.freeSpace());
    ASSERT_FALSE(frame.isDatagramAvailable());
}

TEST(Frame, write_invalid_frame)
{
    std::shared_ptr<MockSocket> io = std::make_shared<MockSocket>();
    Frame frame{PRIMARY_IF_MAC};

    EXPECT_CALL(*io, write(_,ETH_MIN_SIZE))
    .WillOnce(Invoke([](uint8_t const* frame, int32_t)
    {
        {
            EthernetHeader const* header = reinterpret_cast<EthernetHeader const*>(frame);
            EXPECT_EQ(ETH_ETHERCAT_TYPE, header->type);
            for (int32_t i = 0; i < 6; ++i)
            {
                EXPECT_EQ(PRIMARY_IF_MAC[i], header->src_mac[i]);
                EXPECT_EQ(0xff, header->dst_mac[i]);
            }
        }

        {
            EthercatHeader const* header = reinterpret_cast<EthercatHeader const*>(frame + sizeof(EthernetHeader));
            EXPECT_EQ(header->len, 0);
        }
        return ETH_MIN_SIZE;
    }));
    frame.write(io);
}

TEST(Frame, write_multiples_datagrams)
{
    std::shared_ptr<MockSocket> io = std::make_shared<MockSocket>();
    Frame frame{PRIMARY_IF_MAC};

    constexpr int32_t PAYLOAD_SIZE = 32;
    constexpr int32_t EXPECTED_SIZE = sizeof(EthernetHeader) + sizeof(EthercatHeader) + 3 * (sizeof(DatagramHeader) + PAYLOAD_SIZE + ETHERCAT_WKC_SIZE);
    uint8_t buffer[PAYLOAD_SIZE];
    for (int32_t i = 0; i < PAYLOAD_SIZE; ++i)
    {
        buffer[i] = i*i;
    }

    frame.addDatagram(17, Command::FPRD, 20, nullptr, PAYLOAD_SIZE);
    frame.addDatagram(18, Command::BRD,  21, nullptr, PAYLOAD_SIZE);
    frame.addDatagram(19, Command::BWR,  22, buffer,  PAYLOAD_SIZE);
    EXPECT_EQ(3, frame.datagramCounter());

    EXPECT_CALL(*io, write(_, _))
    .WillOnce(Invoke([&](uint8_t const* frame, int32_t frame_size)
    {
        EXPECT_EQ(EXPECTED_SIZE, frame_size);

        {
            EthernetHeader const* header = reinterpret_cast<EthernetHeader const*>(frame);
            EXPECT_EQ(ETH_ETHERCAT_TYPE, header->type);
            for (int32_t i = 0; i < 6; ++i)
            {
                EXPECT_EQ(PRIMARY_IF_MAC[i], header->src_mac[i]);
                EXPECT_EQ(0xff, header->dst_mac[i]);
            }
        }

        {
            EthercatHeader const* header = reinterpret_cast<EthercatHeader const*>(frame + sizeof(EthernetHeader));
            EXPECT_EQ(header->len, 3 * (sizeof(DatagramHeader) + PAYLOAD_SIZE + ETHERCAT_WKC_SIZE));
        }

        for (int32_t i = 0; i < 3; ++i)
        {
            uint8_t const* datagram = reinterpret_cast<uint8_t const*>(frame + sizeof(EthernetHeader) + sizeof(EthercatHeader)
                                                                        + i * (sizeof(DatagramHeader) + PAYLOAD_SIZE + ETHERCAT_WKC_SIZE));
            DatagramHeader const* header = reinterpret_cast<DatagramHeader const*>(datagram);
            uint8_t const* payload = datagram + sizeof(DatagramHeader);
            EXPECT_EQ(17 + i, header->index);
            EXPECT_EQ(20 + i, header->address);
            EXPECT_EQ(PAYLOAD_SIZE, header->len);
            EXPECT_EQ(0, header->IRQ);

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
            }
        }

        return EXPECTED_SIZE;
    }));
    frame.write(io);
}

TEST(Frame, nextDatagram)
{
    Frame frame{PRIMARY_IF_MAC};

    constexpr int32_t PAYLOAD_SIZE = 32;
    uint8_t buffer[PAYLOAD_SIZE];
    for (int32_t i = 0; i < PAYLOAD_SIZE; ++i)
    {
        buffer[i] = i*i;
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
        }
    }
}

TEST(Frame, write_error)
{
    std::shared_ptr<MockSocket> io = std::make_shared<MockSocket>();
    Frame frame{PRIMARY_IF_MAC};

    EXPECT_CALL(*io, write(_,_))
        .WillOnce(Return(-1))
        .WillOnce(Return(0));
    ASSERT_THROW(frame.write(io), std::system_error);
    ASSERT_THROW(frame.write(io), Error);
}

TEST(Frame, read_error)
{
    std::shared_ptr<MockSocket> io = std::make_shared<MockSocket>();
    Frame frame{PRIMARY_IF_MAC};

    EXPECT_CALL(*io, read(_,_))
        .WillOnce(Return(-1))
        .WillOnce(Return(0))
        .WillOnce(Invoke([&](uint8_t* frame, int32_t frame_size)
        {
            EthernetHeader* header = reinterpret_cast<EthernetHeader*>(frame);
            header->type = 0;
            return frame_size;
        }))
        .WillOnce(Return(MAX_ETHERCAT_PAYLOAD_SIZE / 2));
    ASSERT_THROW(frame.read(io), std::system_error);
    ASSERT_THROW(frame.read(io), Error);
    ASSERT_THROW(frame.read(io), Error);
    ASSERT_FALSE(frame.isDatagramAvailable());

    frame.clear();
    frame.addDatagram(0, Command::BRD, 0, nullptr, MAX_ETHERCAT_PAYLOAD_SIZE);
    ASSERT_THROW(frame.read(io), Error);
}


TEST(Frame, isFull_max_datagrams)
{
    Frame frame{PRIMARY_IF_MAC};
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
    Frame frame{PRIMARY_IF_MAC};
    frame.addDatagram(0, Command::BRD, 0, nullptr, MAX_ETHERCAT_PAYLOAD_SIZE);
    ASSERT_TRUE(frame.isFull());
}

TEST(Frame, read_garbage)
{
    std::shared_ptr<MockSocket> io = std::make_shared<MockSocket>();
    Frame frame{PRIMARY_IF_MAC};

    EXPECT_CALL(*io, read(_,ETH_MAX_SIZE)).WillOnce(Return(ETH_MIN_SIZE));
    frame.read(io);
    ASSERT_TRUE(frame.isDatagramAvailable());
}

TEST(Frame, move_constructor)
{
    Frame frameA{PRIMARY_IF_MAC};
    frameA.addDatagram(0, Command::BRD, 0, nullptr, 0);
    frameA.addDatagram(0, Command::BRD, 0, nullptr, 0);

    Frame frameB = std::move(frameA);
    ASSERT_EQ(2, frameB.datagramCounter());
}
