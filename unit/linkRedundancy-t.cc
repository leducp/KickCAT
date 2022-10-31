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

class LinkTest : public testing::Test
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
        io_nominal->checkSendFrame(cmd, to_check, check_payload);
        io_redundancy->checkSendFrame(cmd, to_check, check_payload);
    }

    template<typename T>
    void checkSendFrameRedundancyMultiDTG(std::vector<DatagramCheck<T>> expecteds)
    {
        io_nominal->checkSendFrameMultipleDTG(expecteds);
        io_redundancy->checkSendFrameMultipleDTG(expecteds);
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
    LinkRedundancy link{ io_nominal, io_redundancy, std::bind(&LinkTest::reportRedundancy, this), PRIMARY_IF_MAC, SECONDARY_IF_MAC};

    bool is_redundancy_activated{false};

    int32_t process_callback_counter{0};
    int32_t error_callback_counter{0};
    DatagramState last_error{DatagramState::OK};
};


TEST_F(LinkTest, writeThenRead_NomOK_RedOK)
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

TEST_F(LinkTest, writeThenRead_Nom_NOK_RedOK)
{
    // Case frame are lost between nominal interface and first slave, frame comes back to redundancy interface.

    Frame frame;

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
            return ETH_MIN_SIZE;
        }));
    }

    link.writeThenRead(frame);
}


TEST_F(LinkTest, writeThenRead_NomOK_Red_NOK)
{
    // Case frame are lost between redundancy interface and last slave, frame comes back to nominal interface.

    Frame frame;

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


TEST_F(LinkTest, writeThenRead_NOK)
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

TEST_F(LinkTest, writeThenRead_error_frame_type)
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

TEST_F(LinkTest, writeThenRead_error_wrong_number_bytes_read)
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

TEST_F(LinkTest, isRedundancyNeeded_true)
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

TEST_F(LinkTest, isRedundancyNeeded_false)
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

TEST_F(LinkTest, isRedundancyNeeded_no_interfaces)
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

TEST_F(LinkTest, sendFrame_error_wrong_number_write)
{
    link.addDatagram(Command::BRD, createAddress(0, 0x0000), nullptr, nullptr, nullptr);
    EXPECT_CALL(*io_nominal, write(_,_))
        .WillOnce(Return(0));

    EXPECT_CALL(*io_redundancy, write(_,_))
        .WillOnce(Return(0));

    sendFrame();
    checkSendFrameError();
}

TEST_F(LinkTest, sendFrame_error_write)
{
    link.addDatagram(Command::BRD, createAddress(0, 0x0000), nullptr, nullptr, nullptr);
    EXPECT_CALL(*io_nominal, write(_,_))
        .WillOnce(Return(-1));

    EXPECT_CALL(*io_redundancy, write(_,_))
        .WillOnce(Return(-1));

    sendFrame();
    checkSendFrameError();
}

TEST_F(LinkTest, sendFrame_ok)
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

TEST_F(LinkTest, process_datagrams_line_ok)
{
    InSequence s;

    int64_t skip{0};
    int64_t logical_read = 0x0001020304050607;
    addDatagram(Command::LRD, skip, logical_read, 2, false); // no payload for logical read.
    checkSendFrameRedundancy(Command::LRD, skip, false); // check frame is sent on both interfaces.
    io_redundancy->handleReply<int64_t>({logical_read}, 2);
    io_nominal->handleReply<int64_t>({skip}, 0);

    link.processDatagrams();

    ASSERT_EQ(1, process_callback_counter);
    ASSERT_EQ(0, error_callback_counter);
    ASSERT_EQ(DatagramState::OK, last_error);
}

TEST_F(LinkTest, process_datagrams_nom_cut_red_ok)
{
    InSequence s;

    int64_t skip{0};
    int64_t logical_read = 0x0001020304050607;
    addDatagram(Command::LRD, skip, logical_read, 2, false); // no payload for logical read.
    checkSendFrameRedundancy(Command::LRD, skip, false); // check frame is sent on both interfaces.
    io_redundancy->handleReply<int64_t>({logical_read}, 2);
    io_nominal->readError();

    link.processDatagrams();

    ASSERT_EQ(1, process_callback_counter);
    ASSERT_EQ(0, error_callback_counter);
    ASSERT_EQ(DatagramState::OK, last_error);
}


TEST_F(LinkTest, process_datagrams_nom_ok_red_nok)
{
    InSequence s;

    int64_t skip{0};
    int64_t logical_read = 0x0001020304050607;
    addDatagram(Command::LRD, skip, logical_read, 2, false); // no payload for logical read.
    checkSendFrameRedundancy(Command::LRD, skip, false); // check frame is sent on both interfaces.
    io_redundancy->readError();
    io_nominal->handleReply<int64_t>({logical_read}, 2);

    link.processDatagrams();

    ASSERT_EQ(1, process_callback_counter);
    ASSERT_EQ(0, error_callback_counter);
    ASSERT_EQ(DatagramState::OK, last_error);
}


TEST_F(LinkTest, process_datagrams_both_interfaces_cut)
{
    InSequence s;

    int64_t skip{0};
    int64_t logical_read = 0x0001020304050607;
    addDatagram(Command::LRD, skip, logical_read, 2, false); // no payload for logical read.
    checkSendFrameRedundancy(Command::LRD, skip, false); // check frame is sent on both interfaces.
    io_redundancy->readError();
    io_nominal->readError();

    link.processDatagrams();

    ASSERT_EQ(0, process_callback_counter);
    ASSERT_EQ(1, error_callback_counter);
    ASSERT_EQ(DatagramState::LOST, last_error);
}


TEST_F(LinkTest, process_datagrams_line_cut_between_slaves)
{
    InSequence s;

    int64_t skip{0};
    int64_t logical_read_1 = 0x0001020300000000;
    int64_t logical_read_2 = 0x0000000004050607;
    int64_t logical_read_full = 0x0001020304050607;
    addDatagram(Command::LRD, skip, logical_read_full, 2, false);
    checkSendFrameRedundancy(Command::LRD, skip, false);
    io_redundancy->handleReply<int64_t>({logical_read_2}, 1);
    io_nominal->handleReply<int64_t>({logical_read_1}, 1);

    link.processDatagrams();

    ASSERT_EQ(1, process_callback_counter);
    ASSERT_EQ(0, error_callback_counter);
    ASSERT_EQ(DatagramState::OK, last_error);
}



///////////////////////////////////////////// Write/Read Frame tests ////////////////////////////////////////////////
TEST_F(LinkTest, read_error)
{
    Frame frame;

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
    ASSERT_EQ(readFrame(io_nominal, frame), -1);
    ASSERT_EQ(readFrame(io_nominal, frame), -1);
    ASSERT_FALSE(frame.isDatagramAvailable());

    frame.resetContext();
    frame.addDatagram(0, Command::BRD, 0, nullptr, MAX_ETHERCAT_PAYLOAD_SIZE);
    ASSERT_EQ(readFrame(io_nominal, frame), -1);
}

TEST_F(LinkTest, read_garbage)
{
    Frame frame;

    EXPECT_CALL(*io_nominal, read(_,ETH_MAX_SIZE)).WillOnce(Return(ETH_MIN_SIZE));
    readFrame(io_nominal, frame);
    ASSERT_TRUE(frame.isDatagramAvailable());
}


TEST_F(LinkTest, write_invalid_frame)
{
    Frame frame;

    EXPECT_CALL(*io_nominal, write(_,ETH_MIN_SIZE))
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
    writeFrame(io_nominal, frame, PRIMARY_IF_MAC);
}


TEST_F(LinkTest, write_multiples_datagrams)
{
    Frame frame;

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
                EXPECT_EQ(PRIMARY_IF_MAC[i], header->src_mac[i]);
                EXPECT_EQ(0xff, header->dst_mac[i]);
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
                default: {}
            }
        }

        return EXPECTED_SIZE;
    }));
    writeFrame(io_nominal, frame, PRIMARY_IF_MAC);
}

TEST_F(LinkTest, write_error)
{
    Frame frame;

    EXPECT_CALL(*io_nominal, write(_,_))
        .WillOnce(Return(-1))
        .WillOnce(Return(0));
    ASSERT_THROW(writeFrame(io_nominal, frame, PRIMARY_IF_MAC), std::system_error);
    ASSERT_THROW(writeFrame(io_nominal, frame, PRIMARY_IF_MAC), Error);
}

///////////////////////////////////////////// END Write/Read Frame tests /////////////////////////////////////////////


TEST_F(LinkTest, add_datagram_more_than_15)
{
    uint8_t data = 12;
    Command cmd = Command::BWR;
    std::vector<DatagramCheck<uint8_t>> expecteds_15(15, {cmd, data});
    std::vector<DatagramCheck<uint8_t>> expecteds_5(5, {cmd, data});
    {
        InSequence s;

        checkSendFrameRedundancyMultiDTG(expecteds_15);
        checkSendFrameRedundancyMultiDTG(expecteds_5);
    }

    for (int32_t i=0; i<20; ++i)
    {
        addDatagram(cmd, data, data, 0);
    }

    link.finalizeDatagrams();

    ASSERT_EQ(0, process_callback_counter);
    ASSERT_EQ(0, error_callback_counter);
}


TEST_F(LinkTest, add_big_datagram)
{
    uint8_t data = 3;
    Command cmd = Command::BWR;
    std::vector<DatagramCheck<uint8_t>> expecteds_5(5, {cmd, data});

    std::array<uint8_t, MAX_ETHERCAT_PAYLOAD_SIZE> big_payload;
    std::fill(std::begin(big_payload), std::end(big_payload), 2);

    std::vector<DatagramCheck<std::array<uint8_t, MAX_ETHERCAT_PAYLOAD_SIZE>>> expecteds_big(1, {cmd, big_payload});

    {
        InSequence s;

        checkSendFrameRedundancyMultiDTG(expecteds_5);
        checkSendFrameRedundancyMultiDTG(expecteds_big);
    }

    for (int32_t i=0; i<5; ++i)
    {
        addDatagram(cmd, data, data, 0);
    }
    addDatagram(cmd, big_payload, big_payload, 0);

    link.finalizeDatagrams();
}


TEST_F(LinkTest, add_too_many_datagrams)
{
    uint8_t data = 3;
    Command cmd = Command::BWR;
    std::vector<DatagramCheck<uint8_t>> expecteds_15(15, {cmd, data});

    constexpr int32_t SEND_DATAGRAMS_OK = 255;
    {
        InSequence s;

        for (int32_t i = 0; i < (SEND_DATAGRAMS_OK / 15); ++i)
        {
            checkSendFrameRedundancyMultiDTG(expecteds_15);
        }
    }


    for (int32_t i=0; i < SEND_DATAGRAMS_OK; ++i)
    {
        addDatagram(cmd, data, data, 0);
    }
    EXPECT_THROW(addDatagram(cmd, data, data, 0), Error);
    link.finalizeDatagrams();
}


TEST_F(LinkTest, process_datagrams_nothing_to_do)
{
    link.processDatagrams();
    ASSERT_EQ(0, process_callback_counter);
    ASSERT_EQ(0, error_callback_counter);
    ASSERT_EQ(DatagramState::OK, last_error);
}


TEST_F(LinkTest, process_datagrams_invalid_frame)
{
    uint8_t payload = 3;
    Command cmd = Command::BWR;
    std::vector<DatagramCheck<uint8_t>> expecteds_1(1, {cmd, payload});
    checkSendFrameRedundancyMultiDTG(expecteds_1);

    addDatagram(cmd, payload, payload, 0);

    EXPECT_CALL(*io_nominal, read(_,_))
    .WillOnce(Invoke([](uint8_t* data, int32_t)
    {
        std::memset(data, 0, ETH_MIN_SIZE);
        return ETH_MIN_SIZE;
    }));

    EXPECT_CALL(*io_redundancy, read(_,_))
    .WillOnce(Invoke([](uint8_t* data, int32_t)
    {
        std::memset(data, 0, ETH_MIN_SIZE);
        return ETH_MIN_SIZE;
    }));

    link.processDatagrams();

    ASSERT_EQ(0, process_callback_counter);
    ASSERT_EQ(1, error_callback_counter);    // datagram lost (invalid frame)
    ASSERT_EQ(DatagramState::LOST, last_error);
}


TEST_F(LinkTest, process_datagrams_invalid_size)
{
    uint8_t payload = 3;
    Command cmd = Command::BWR;
    std::vector<DatagramCheck<uint8_t>> expecteds_1(1, {cmd, payload});
    checkSendFrameRedundancyMultiDTG(expecteds_1);

    addDatagram(cmd, payload, payload, 0);

    EXPECT_CALL(*io_nominal, read(_,_))
    .WillOnce(Invoke([](uint8_t*, int32_t)
    {
        return 1;
    }));

    EXPECT_CALL(*io_redundancy, read(_,_))
    .WillOnce(Invoke([](uint8_t*, int32_t)
    {
        return 1;
    }));

    link.processDatagrams();

    ASSERT_EQ(0, process_callback_counter);
    ASSERT_EQ(1, error_callback_counter);    // datagram lost (invalid frame)
    ASSERT_EQ(DatagramState::LOST, last_error);
}


TEST_F(LinkTest, process_datagrams_send_error)
{
    EXPECT_CALL(*io_nominal, write(_,_))
    .WillOnce(Invoke([](uint8_t const*, int32_t)
    {
        return 1;
    }));

    EXPECT_CALL(*io_redundancy, write(_,_))
    .WillOnce(Invoke([](uint8_t const*, int32_t)
    {
        return 1;
    }));

    uint8_t payload = 3;
    addDatagram(Command::BWR, payload, payload, 0);

    ASSERT_NO_THROW(link.processDatagrams());

    ASSERT_EQ(0, process_callback_counter);
    ASSERT_EQ(1, error_callback_counter);   // datagram lost (sent error)
    ASSERT_EQ(DatagramState::SEND_ERROR, last_error);
}


TEST_F(LinkTest, process_datagrams_error_rethrow)
{
    uint8_t payload;
    Command cmd = Command::BRD;
    std::vector<DatagramCheck<uint8_t>> expecteds_5(1, {cmd, payload});

    checkSendFrameRedundancyMultiDTG(expecteds_5);

    link.addDatagram(Command::BRD, 0, payload,
        [&](DatagramHeader const*, uint8_t const*, uint16_t) { return DatagramState::INVALID_WKC; },
        [&](DatagramState const&){ throw std::runtime_error("A"); }
    );
    link.addDatagram(Command::BRD, 0, payload,
        [&](DatagramHeader const*, uint8_t const*, uint16_t) { return DatagramState::INVALID_WKC; },
        [&](DatagramState const&){ throw std::out_of_range("B"); }
    );
    link.addDatagram(Command::BRD, 0, payload,
        [&](DatagramHeader const*, uint8_t const*, uint16_t) { return DatagramState::INVALID_WKC; },
        [&](DatagramState const&){ throw std::logic_error("C"); }
    );
    link.addDatagram(Command::BRD, 0, payload,
        [&](DatagramHeader const*, uint8_t const*, uint16_t) { return DatagramState::INVALID_WKC; },
        [&](DatagramState const&){ throw std::overflow_error("D"); }
    );
    link.addDatagram(Command::BRD, 0, payload,
        [&](DatagramHeader const*, uint8_t const*, uint16_t) { return DatagramState::OK; },
        [&](DatagramState const&){ throw std::underflow_error("E"); }
    );

    EXPECT_CALL(*io_nominal, read(_,_))
    .WillOnce(Invoke([&](uint8_t*, int32_t)
    {
        return ETH_MIN_SIZE;
    }));
    EXPECT_CALL(*io_redundancy, read(_,_))
    .WillOnce(Invoke([&](uint8_t*, int32_t)
    {
        return ETH_MIN_SIZE;
    }));

    EXPECT_THROW(link.processDatagrams(), std::overflow_error);
}


}
