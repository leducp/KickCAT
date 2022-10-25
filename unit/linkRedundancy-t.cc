#include <gtest/gtest.h>
#include <cstring>
#include <string.h>

#include "Mocks.h"
#include "kickcat/LinkRedundancy.h"

using ::testing::Return;
using ::testing::_;
using ::testing::Invoke;
using ::testing::InSequence;

namespace kickcat
{

class LinkRedTest : public testing::Test
{
public:
    void reportRedundancy()
    {
        printf("Redundancy has been activated\n");
        is_redundancy_activated = true;
    }

    void sendFrame()
    {
        link.sendFrame();
    }

    void checkSendFrameError()
    {
        ASSERT_EQ(link.sent_frame_, 0);
        ASSERT_EQ(link.callbacks_[0].status, DatagramState::SEND_ERROR);
    }

    template<typename T>
    void checkSendFrameRedundancy(Command cmd, T to_check, bool check_payload = true)
    {
        mockBus.checkSendFrame(io_nominal, cmd, to_check, check_payload);
        mockBus.checkSendFrame(io_redundancy, cmd, to_check, check_payload);
    }

    template<typename T, typename U>
    void addDatagram(Command cmd, T& payload, U& expected_data, uint16_t expected_wkc, bool error = false)
    {
        link.addDatagram(cmd, 0, payload,
        [&, error, expected_wkc, cmd](DatagramHeader const* header, uint8_t const* data, uint16_t wkc)
        {
            process_callback_counter++;

            if (error)
            {
                std::cout << "ERROR " << std::endl;
                return DatagramState::INVALID_WKC;
            }

            EXPECT_EQ(wkc, expected_wkc);
            EXPECT_EQ(0, std::memcmp(data, &expected_data, sizeof(expected_data)));
            EXPECT_EQ(sizeof(expected_data), header->len);
            EXPECT_EQ(cmd, header->command);

            return DatagramState::OK;
        },
        [&](DatagramState const& status)
        {
            error_callback_counter++;
            last_error = status;
        });
    }

protected:
    std::shared_ptr<MockSocket> io_nominal{ std::make_shared<MockSocket>() };
    std::shared_ptr<MockSocket> io_redundancy{ std::make_shared<MockSocket>() };
    LinkRedundancy link{ io_nominal, io_redundancy, std::bind(&LinkRedTest::reportRedundancy, this), PRIMARY_IF_MAC, SECONDARY_IF_MAC};

    MockBus mockBus;

    bool is_redundancy_activated{false};

    int32_t process_callback_counter{0};
    int32_t error_callback_counter{0};
    DatagramState last_error{DatagramState::OK};
};


TEST_F(LinkRedTest, writeThenRead_NomOK_RedOK)
{
    // Case we can read on both interface, either there is no line cut, either the cut is between two slaves.

    Frame frame;

    EXPECT_CALL(*io_redundancy, write(_,_))
    .WillOnce(Invoke([&](uint8_t const*, int32_t)
    {
        return ETH_MIN_SIZE;
    }));

    EXPECT_CALL(*io_nominal, read(_,_))
    .WillOnce(Invoke([](uint8_t*, int32_t)
    {
        return ETH_MIN_SIZE;
    }));

    EXPECT_CALL(*io_nominal, write(_,_))
    .WillOnce(Invoke([&](uint8_t const*, int32_t)
    {
        return ETH_MIN_SIZE;
    }));

    EXPECT_CALL(*io_redundancy, read(_,_))
    .WillOnce(Invoke([](uint8_t*, int32_t)
    {
        return ETH_MIN_SIZE;
    }));

    link.writeThenRead(frame);
}

TEST_F(LinkRedTest, writeThenRead_Nom_NOK_RedOK)
{
    // Case frame are lost between nominal interface and first slave, frame comes back to redundancy interface.

    Frame frame;

// case interface Nom is down in write ? , to test on bench TODO
    {
      InSequence seq;

        // Write into nominal interface
        EXPECT_CALL(*io_nominal, write(_,_))
        .WillOnce(Invoke([&](uint8_t const*, int32_t)
        {
            return ETH_MIN_SIZE;  // 0 ?
        }));

        EXPECT_CALL(*io_redundancy, read(_,_))
        .WillOnce(Invoke([](uint8_t*, int32_t)
        {
            return 0;
        }));

        EXPECT_CALL(*io_nominal, read(_,_))
        .WillOnce(Invoke([](uint8_t*, int32_t)
        {
            return 0;
        }));

        // Write into redundancy interface
        EXPECT_CALL(*io_redundancy, write(_,_))
        .WillOnce(Invoke([&](uint8_t const*, int32_t)
        {
            return ETH_MIN_SIZE;
        }));

        EXPECT_CALL(*io_nominal, read(_,_))
        .WillOnce(Invoke([](uint8_t*, int32_t)
        {
            return 0;
        }));

        EXPECT_CALL(*io_redundancy, read(_,_))
        .WillOnce(Invoke([](uint8_t*, int32_t)
        {
            return ETH_MIN_SIZE;
        }));
    }

    link.writeThenRead(frame);
}


TEST_F(LinkRedTest, writeThenRead_NomOK_Red_NOK)
{
    // Case frame are lost between redundancy interface and last slave, frame comes back to nominal interface.

    Frame frame;

// case interface Nom is down in write ? , to test on bench TODO
    {
      InSequence seq;

        // Write into nominal interface
        EXPECT_CALL(*io_nominal, write(_,_))
        .WillOnce(Invoke([&](uint8_t const*, int32_t)
        {
            return ETH_MIN_SIZE;
        }));

        EXPECT_CALL(*io_redundancy, read(_,_))
        .WillOnce(Invoke([](uint8_t*, int32_t)
        {
            return 0;
        }));

        EXPECT_CALL(*io_nominal, read(_,_))
        .WillOnce(Invoke([](uint8_t*, int32_t)
        {
            return ETH_MIN_SIZE;
        }));

        // Write into redundancy interface
        EXPECT_CALL(*io_redundancy, write(_,_))
        .WillOnce(Invoke([&](uint8_t const*, int32_t)
        {
            return ETH_MIN_SIZE;
        }));

        EXPECT_CALL(*io_nominal, read(_,_))
        .WillOnce(Invoke([](uint8_t*, int32_t)
        {
            return 0;
        }));

        EXPECT_CALL(*io_redundancy, read(_,_))
        .WillOnce(Invoke([](uint8_t*, int32_t)
        {
            return 0;
        }));
    }

    link.writeThenRead(frame);
}


TEST_F(LinkRedTest, writeThenRead_NOK)
{
    // Case both interfaces can't read frames.

    Frame frame;

// case interface Nom is down in write ? , to test on bench TODO
    {
      InSequence seq;

        // Write into nominal interface
        EXPECT_CALL(*io_nominal, write(_,_))
        .WillOnce(Invoke([&](uint8_t const*, int32_t)
        {
            return ETH_MIN_SIZE;
        }));

        EXPECT_CALL(*io_redundancy, read(_,_))
        .WillOnce(Invoke([](uint8_t*, int32_t)
        {
            return 0;
        }));

        EXPECT_CALL(*io_nominal, read(_,_))
        .WillOnce(Invoke([](uint8_t*, int32_t)
        {
            return 0;
        }));

        // Write into redundancy interface
        EXPECT_CALL(*io_redundancy, write(_,_))
        .WillOnce(Invoke([&](uint8_t const*, int32_t)
        {
            return ETH_MIN_SIZE;
        }));

        EXPECT_CALL(*io_nominal, read(_,_))
        .WillOnce(Invoke([](uint8_t*, int32_t)
        {
            return 0;
        }));

        EXPECT_CALL(*io_redundancy, read(_,_))
        .WillOnce(Invoke([](uint8_t*, int32_t)
        {
            return 0;
        }));
    }

    ASSERT_THROW(link.writeThenRead(frame), std::system_error);
}

TEST_F(LinkRedTest, writeThenRead_error_frame_type)
{
    Frame frame;
    EXPECT_CALL(*io_redundancy, write(_,_))
    .WillOnce(Invoke([&](uint8_t const*, int32_t)
    {
        return ETH_MIN_SIZE;
    }));

    EXPECT_CALL(*io_nominal, read(_,_))
    .WillOnce(Invoke([&](uint8_t* frame_in, int32_t)
    {
        EthernetHeader* header = reinterpret_cast<EthernetHeader*>(frame_in);
        header->type = 0;
        return ETH_MIN_SIZE;
    }));

    EXPECT_CALL(*io_nominal, write(_,_))
    .WillOnce(Invoke([&](uint8_t const*, int32_t)
    {
        return ETH_MIN_SIZE;
    }));

    EXPECT_CALL(*io_redundancy, read(_,_))
    .WillOnce(Invoke([&](uint8_t* frame_in, int32_t)
    {
        EthernetHeader* header = reinterpret_cast<EthernetHeader*>(frame_in);
        header->type = 0;
        return ETH_MIN_SIZE;
    }));

    ASSERT_THROW(link.writeThenRead(frame), Error);
}

TEST_F(LinkRedTest, writeThenRead_error_wrong_number_bytes_read)
{
    Frame frame;
    EXPECT_CALL(*io_redundancy, write(_,_))
    .WillOnce(Invoke([&](uint8_t const*, int32_t)
    {
        return ETH_MIN_SIZE;
    }));

    EXPECT_CALL(*io_nominal, read(_,_))
    .WillOnce(Invoke([&](uint8_t* frame_in, int32_t)
    {
        EthercatHeader* header = reinterpret_cast<EthercatHeader*>(frame_in + sizeof(EthernetHeader));
        header->len = ETH_MAX_SIZE;
        return ETH_MAX_SIZE;
    }));

    EXPECT_CALL(*io_nominal, write(_,_))
    .WillOnce(Invoke([&](uint8_t const*, int32_t)
    {
        return ETH_MIN_SIZE;
    }));

    EXPECT_CALL(*io_redundancy, read(_,_))
    .WillOnce(Invoke([&](uint8_t* frame_in, int32_t)
    {
        EthercatHeader* header = reinterpret_cast<EthercatHeader*>(frame_in + sizeof(EthernetHeader));
        header->len = ETH_MAX_SIZE;
        return ETH_MAX_SIZE;
    }));

    ASSERT_THROW(link.writeThenRead(frame), Error);
}

TEST_F(LinkRedTest, isRedundancyNeeded_true)
{
    EXPECT_CALL(*io_redundancy, write(_,_))
    .WillOnce(Invoke([&](uint8_t const*, int32_t)
    {
        return ETH_MIN_SIZE;
    }));

    EXPECT_CALL(*io_nominal, read(_,_))
    .WillOnce(Invoke([](uint8_t*, int32_t)
    {
        // The frame didn't reach the nominal interface.
        return -1;
    }));

    EXPECT_CALL(*io_redundancy, read(_,_))
    .WillOnce(Invoke([](uint8_t* frame_in, int32_t)
    {
        // Add a datagram to the read frame and set its working counter to 1.
        Frame frame;
        frame.addDatagram(0, Command::BRD,  0, nullptr, 1);
        int32_t toWrite = frame.finalize();
        std::memcpy(frame_in, frame.data(), toWrite);
        uint8_t* datagram_addr = frame_in + sizeof(EthernetHeader) + sizeof(EthercatHeader);
        DatagramHeader const* header = reinterpret_cast<DatagramHeader*>(datagram_addr);
        uint8_t* wkc_addr = datagram_addr + sizeof(DatagramHeader) + header->len;
        uint16_t wkc = 1;
        std::memcpy(wkc_addr, &wkc, sizeof(uint16_t));
        return toWrite;
    }));

    link.checkRedundancyNeeded();

    ASSERT_EQ(is_redundancy_activated, true);
}

TEST_F(LinkRedTest, isRedundancyNeeded_false)
{
    EXPECT_CALL(*io_redundancy, write(_,_))
    .WillOnce(Invoke([&](uint8_t const*, int32_t)
    {
        return ETH_MIN_SIZE;
    }));

    EXPECT_CALL(*io_nominal, read(_,_))
    .WillOnce(Invoke([](uint8_t*, int32_t)
    {
        return ETH_MIN_SIZE;
    }));

    // The read frame has a working counter of 0
    link.checkRedundancyNeeded();

    ASSERT_EQ(is_redundancy_activated, false);
}

TEST_F(LinkRedTest, isRedundancyNeeded_no_interfaces)
{
    EXPECT_CALL(*io_redundancy, write(_,_))
    .WillOnce(Invoke([&](uint8_t const*, int32_t)
    {
        return ETH_MIN_SIZE;
    }));

    EXPECT_CALL(*io_nominal, read(_,_))
    .WillOnce(Invoke([](uint8_t*, int32_t)
    {
        return -1;
    }));

    EXPECT_CALL(*io_redundancy, read(_,_))
    .WillOnce(Invoke([](uint8_t*, int32_t)
    {
        return -1;
    }));

    // The read frame has a working counter of 0
    link.checkRedundancyNeeded();

    ASSERT_EQ(is_redundancy_activated, false);
}

TEST_F(LinkRedTest, sendFrame_error_wrong_number_write)
{
    link.addDatagram(Command::BRD, createAddress(0, 0x0000), nullptr, nullptr, nullptr);
    EXPECT_CALL(*io_nominal, write(_,_))
        .WillOnce(Return(0));

    EXPECT_CALL(*io_redundancy, write(_,_))
        .WillOnce(Return(0));

    sendFrame();
    checkSendFrameError();
}

TEST_F(LinkRedTest, sendFrame_error_write)
{
    link.addDatagram(Command::BRD, createAddress(0, 0x0000), nullptr, nullptr, nullptr);
    EXPECT_CALL(*io_nominal, write(_,_))
        .WillOnce(Return(-1));

    EXPECT_CALL(*io_redundancy, write(_,_))
        .WillOnce(Return(-1));

    sendFrame();
    checkSendFrameError();
}

TEST_F(LinkRedTest, sendFrame_ok)
{
    EXPECT_CALL(*io_nominal, write(_,_))
    .WillOnce(Invoke([](uint8_t const* frame_in, int32_t)
    {
        {
            EthernetHeader const* header = reinterpret_cast<EthernetHeader const*>(frame_in);
            EXPECT_EQ(ETH_ETHERCAT_TYPE, header->type);
            for (int32_t i = 0; i < 6; ++i)
            {
                EXPECT_EQ(PRIMARY_IF_MAC[i], header->src_mac[i]);
                EXPECT_EQ(0xff, header->dst_mac[i]);
            }
        }

        {
            EthercatHeader const* header = reinterpret_cast<EthercatHeader const*>(frame_in + sizeof(EthernetHeader));
            EXPECT_EQ(header->len, 0);
        }
        return ETH_MIN_SIZE;
    }));

    EXPECT_CALL(*io_redundancy, write(_,_))
    .WillOnce(Invoke([](uint8_t const* frame_in, int32_t)
    {
        {
            EthernetHeader const* header = reinterpret_cast<EthernetHeader const*>(frame_in);
            EXPECT_EQ(ETH_ETHERCAT_TYPE, header->type);
            for (int32_t i = 0; i < 6; ++i)
            {
                EXPECT_EQ(SECONDARY_IF_MAC[i], header->src_mac[i]);
                EXPECT_EQ(0xff, header->dst_mac[i]);
            }
        }

        {
            EthercatHeader const* header = reinterpret_cast<EthercatHeader const*>(frame_in + sizeof(EthernetHeader));
            EXPECT_EQ(header->len, 0);
        }
        return ETH_MIN_SIZE;
    }));

    sendFrame();
}

TEST_F(LinkRedTest, process_datagrams_OK)
{
    InSequence s;

    int64_t skip{1234};
    int64_t logical_read = 0x0001020304050607;
    addDatagram(Command::LRD, skip, logical_read, 2, false); // no payload for logical read.
    checkSendFrameRedundancy(Command::LRD, skip, false); // check frame is sent on both interfaces.
    mockBus.handleReply<int64_t>(io_redundancy, {logical_read}, 2);
    mockBus.handleReply<int64_t>(io_nominal, {skip}, 0);

    link.processDatagrams();

    ASSERT_EQ(1, process_callback_counter);
    ASSERT_EQ(0, error_callback_counter);
    ASSERT_EQ(DatagramState::OK, last_error);
}

// TODO fail read
// TODO test fusion datagram

}
