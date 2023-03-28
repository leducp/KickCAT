#include <gtest/gtest.h>
#include <cstring>

#include "kickcat/Link.h"
#include "Mocks.h"

using ::testing::Return;
using ::testing::_;
using ::testing::Invoke;
using ::testing::InSequence;

namespace kickcat
{

class LinkTest : public testing::Test
{
public:

    void SetUp() override
    {
        EXPECT_CALL(*io_nominal, setTimeout(::testing::_))
            .WillRepeatedly(Return());
        EXPECT_CALL(*io_redundancy, setTimeout(::testing::_))
                    .WillRepeatedly(Return());
    }

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
    void checkSendFrameRedundancy(std::vector<DatagramCheck<T>> expecteds)
    {
        io_nominal->checkSendFrame(expecteds);
        io_redundancy->checkSendFrame(expecteds);
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
    Link link{ io_nominal, io_redundancy, std::bind(&LinkTest::reportRedundancy, this), PRIMARY_IF_MAC, SECONDARY_IF_MAC};

    bool is_redundancy_activated{false};

    int32_t process_callback_counter{0};
    int32_t error_callback_counter{0};
    DatagramState last_error{DatagramState::OK};
};


TEST_F(LinkTest, writeThenRead_NomOK_RedOK)
{
    // Case we can read on both interface, either there is no line cut, either the cut is between two slaves.

    Frame frame;

    nanoseconds timeout = 10ms;
    link.setTimeout(timeout);

    EXPECT_CALL(*io_nominal, setTimeout(timeout));
    EXPECT_CALL(*io_redundancy, setTimeout(timeout));

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

TEST_F(LinkTest, writeThenRead_error_write)
{
    Frame frame;
    EXPECT_CALL(*io_nominal, write(_,_))
    .WillOnce(Invoke([&](uint8_t const*, int32_t)
    {
        return -1;
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

    EXPECT_CALL(*io_redundancy, write(_,_))
    .WillOnce(Invoke([](uint8_t const* frame_in, int32_t)
    {
        {
            EthernetHeader const* header = reinterpret_cast<EthernetHeader const*>(frame_in);
            EXPECT_EQ(ETH_ETHERCAT_TYPE, header->type);
            for (int32_t i = 0; i < 6; ++i)
            {
                EXPECT_EQ(SECONDARY_IF_MAC[i], header->src[i]);
                EXPECT_EQ(0xff, header->dst[i]);
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
    Command cmd = Command::LRD;
    std::vector<DatagramCheck<int64_t>> expecteds_1(1, {cmd, skip, false}); // no payload for logical read.
    addDatagram(cmd, skip, logical_read, 2, false);
    checkSendFrameRedundancy(expecteds_1); // check frame is sent on both interfaces.

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
    Command cmd = Command::LRD;
    std::vector<DatagramCheck<int64_t>> expecteds_1(1, {cmd, skip, false}); // no payload for logical read.
    addDatagram(cmd, skip, logical_read, 2, false);
    checkSendFrameRedundancy(expecteds_1); // check frame is sent on both interfaces.
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
    Command cmd = Command::LRD;
    std::vector<DatagramCheck<int64_t>> expecteds_1(1, {cmd, skip, false}); // no payload for logical read.
    addDatagram(Command::LRD, skip, logical_read, 2, false);
    checkSendFrameRedundancy(expecteds_1); // check frame is sent on both interfaces.
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
    Command cmd = Command::LRD;
    std::vector<DatagramCheck<int64_t>> expecteds_1(1, {cmd, skip, false}); // no payload for logical read.
    addDatagram(cmd, skip, logical_read, 2, false);
    checkSendFrameRedundancy(expecteds_1); // check frame is sent on both interfaces.
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
    Command cmd = Command::LRD;
    std::vector<DatagramCheck<int64_t>> expecteds_1(1, {cmd, skip, false}); // no payload for logical read.
    addDatagram(cmd, skip, logical_read_full, 2, false);
    checkSendFrameRedundancy(expecteds_1); // check frame is sent on both interfaces.
    io_redundancy->handleReply<int64_t>({logical_read_2}, 1);
    io_nominal->handleReply<int64_t>({logical_read_1}, 1);

    link.processDatagrams();

    ASSERT_EQ(1, process_callback_counter);
    ASSERT_EQ(0, error_callback_counter);
    ASSERT_EQ(DatagramState::OK, last_error);
}


TEST_F(LinkTest, process_datagrams_multiple_frames)
{
    InSequence s;

    int64_t skip{0};
    int64_t logical_read = 1111;
    Command cmd = Command::LRD;
    int32_t datagram_number = 15;
    std::vector<DatagramCheck<int64_t>> expecteds(datagram_number, {cmd, skip, false}); // no payload for logical read.
    std::vector<int64_t> answers(datagram_number, logical_read);
    std::vector<int64_t> skips(datagram_number, skip);

    int32_t frame_number = 14;

    for (int32_t i = 0; i < frame_number; i++)
    {
        checkSendFrameRedundancy(expecteds); // check frame is sent on both interfaces.
    }

    for (int32_t i = 0; i < frame_number; i++)
    {
        for (int32_t j = 0; j < datagram_number; j++)
        {
            addDatagram(cmd, skip, logical_read, 2, false);
        }

        io_redundancy->handleReply<int64_t>(answers, 2);
        io_nominal->handleReply<int64_t>(skips, 0);
    }

    link.processDatagrams();

    ASSERT_EQ(datagram_number * frame_number, process_callback_counter);
    ASSERT_EQ(0, error_callback_counter);
    ASSERT_EQ(DatagramState::OK, last_error);
}


TEST_F(LinkTest, process_datagrams_multiple_frames_split)
{
    InSequence s;

    int64_t skip{0};
    int64_t logical_read = 1111;
    Command cmd = Command::LRD;
    std::vector<DatagramCheck<int64_t>> expecteds_15(15, {cmd, skip, false}); // no payload for logical read.
    std::vector<int64_t> answers_15(15, logical_read);
    std::vector<int64_t> skips_15(15, skip);

    std::vector<DatagramCheck<int64_t>> expecteds_4(4, {cmd, skip, false}); // no payload for logical read.
    std::vector<int64_t> answers_4(4, logical_read);
    std::vector<int64_t> skips_4(4, skip);

    checkSendFrameRedundancy(expecteds_15);
    checkSendFrameRedundancy(expecteds_4);

    for (int32_t j = 0; j < 19; j++)
    {
        addDatagram(cmd, skip, logical_read, 2, false);
    }

    io_redundancy->handleReply<int64_t>(answers_15, 2);
    io_nominal->handleReply<int64_t>(skips_15, 0);
    io_redundancy->handleReply<int64_t>(answers_4, 2);
    io_nominal->handleReply<int64_t>(skips_4, 0);

    link.processDatagrams();

    ASSERT_EQ(19, process_callback_counter);
    ASSERT_EQ(0, error_callback_counter);
    ASSERT_EQ(DatagramState::OK, last_error);
}


TEST_F(LinkTest, process_big_datagram_multiframe)
{
    uint8_t data = 3;
    uint8_t skip{0};
    Command cmd = Command::BWR;
    std::vector<DatagramCheck<uint8_t>> expecteds_5(5, {cmd, data});
    std::vector<uint8_t> answers_5(5, data);
    std::vector<uint8_t> skips_5(5, skip);

    std::array<uint8_t, MAX_ETHERCAT_PAYLOAD_SIZE> big_payload;
    std::array<uint8_t, MAX_ETHERCAT_PAYLOAD_SIZE> big_skip;
    std::fill(std::begin(big_payload), std::end(big_payload), 2);
    std::fill(std::begin(big_skip), std::end(big_skip), 0);

    std::vector<DatagramCheck<std::array<uint8_t, MAX_ETHERCAT_PAYLOAD_SIZE>>> expecteds_big(1, {cmd, big_payload});
    std::vector<std::array<uint8_t, MAX_ETHERCAT_PAYLOAD_SIZE>> answers_big(1, big_payload);
    std::vector<std::array<uint8_t, MAX_ETHERCAT_PAYLOAD_SIZE>> skips_big(1, big_skip);

    InSequence s;

    checkSendFrameRedundancy(expecteds_5);
    checkSendFrameRedundancy(expecteds_big);

    for (int32_t i=0; i<5; ++i)
    {
        addDatagram(cmd, data, data, 2);
    }
    addDatagram(cmd, big_payload, big_payload, 2);

    io_redundancy->handleReply<uint8_t>(answers_5, 2);
    io_nominal->handleReply<uint8_t>(skips_5, 0);
    io_redundancy->handleReply<std::array<uint8_t, MAX_ETHERCAT_PAYLOAD_SIZE>>(answers_big, 2);
    io_nominal->handleReply<std::array<uint8_t, MAX_ETHERCAT_PAYLOAD_SIZE>>(skips_big, 0);

    link.processDatagrams();

    ASSERT_EQ(6, process_callback_counter);
    ASSERT_EQ(0, error_callback_counter);
    ASSERT_EQ(DatagramState::OK, last_error);
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
            checkSendFrameRedundancy(expecteds_15);
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
    checkSendFrameRedundancy(expecteds_1);

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
    checkSendFrameRedundancy(expecteds_1);

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
    uint8_t payload = 0;
    Command cmd = Command::BRD;
    std::vector<DatagramCheck<uint8_t>> expecteds_5(5, {cmd, payload, false});

    checkSendFrameRedundancy(expecteds_5);

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


TEST_F(LinkTest, process_datagrams_old_frame)
{
    uint8_t payload = 0;
    Command cmd = Command::BRD;
    std::vector<DatagramCheck<uint8_t>> expecteds_1(1, {cmd, payload, false});

    // first frame - lost
    checkSendFrameRedundancy(expecteds_1);
    addDatagram(cmd, payload, payload, 0);

    EXPECT_CALL(*io_nominal, read(_,_))
    .WillOnce(Invoke([](uint8_t*, int32_t)
    {
        errno = EAGAIN;
        return -1;
    })).RetiresOnSaturation();

    EXPECT_CALL(*io_redundancy, read(_,_))
    .WillOnce(Invoke([](uint8_t*, int32_t)
    {
        errno = EAGAIN;
        return -1;
    })).RetiresOnSaturation();

    link.processDatagrams();

    ASSERT_EQ(0, process_callback_counter); // datagram lost (invalid frame)
    ASSERT_EQ(1, error_callback_counter);

    // second frame - the previous one that was not THAT lost
    checkSendFrameRedundancy(expecteds_1);
    addDatagram(cmd, payload, payload, 0);

    {
        InSequence s;

        // handle only response on nominal
        EXPECT_CALL(*io_redundancy, read(_,_))
        .WillOnce(Invoke([](uint8_t*, int32_t)
        {
            return -1;
        })).RetiresOnSaturation();

        EXPECT_CALL(*io_nominal, read(_,_))
        .WillOnce(Invoke([](uint8_t* data, int32_t)
        {
            Frame frame;
            frame.addDatagram(0, Command::BRD,  0, nullptr, 1);
            int32_t toWrite = frame.finalize();
            std::memcpy(data, frame.data(), toWrite);
            return toWrite;
        })).RetiresOnSaturation();


        EXPECT_CALL(*io_redundancy, read(_,_))
        .WillOnce(Invoke([](uint8_t*, int32_t)
        {
            return -1;
        })).RetiresOnSaturation();

        EXPECT_CALL(*io_nominal, read(_,_))
        .WillOnce(Invoke([](uint8_t*, int32_t)
        {
            errno = EAGAIN;
            return -1;
        })).RetiresOnSaturation();
    }
    link.processDatagrams();

    ASSERT_EQ(0, process_callback_counter); // datagram lost (invalid frame)
    ASSERT_EQ(2, error_callback_counter);

    // third frame: read wrong frame but read the right one afterward
    checkSendFrameRedundancy(expecteds_1);
    addDatagram(cmd, payload, payload, 0);

    {
        InSequence s;

        // handle only response on nominal
        EXPECT_CALL(*io_redundancy, read(_,_))
        .WillOnce(Invoke([](uint8_t*, int32_t)
        {
            return -1;
        })).RetiresOnSaturation();

        EXPECT_CALL(*io_nominal, read(_,_))
        .WillOnce(Invoke([](uint8_t* data, int32_t)
        {
            Frame frame;
            frame.addDatagram(1, Command::BRD,  0, nullptr, 1);
            int32_t toWrite = frame.finalize();
            std::memcpy(data, frame.data(), toWrite);
            return toWrite;
        })).RetiresOnSaturation();

        // handle only response on nominal
        EXPECT_CALL(*io_redundancy, read(_,_))
        .WillOnce(Invoke([](uint8_t*, int32_t)
        {
            return -1;
        })).RetiresOnSaturation();

        EXPECT_CALL(*io_nominal, read(_,_))
        .WillOnce(Invoke([payload](uint8_t* data, int32_t)
        {
            Frame frame;
            frame.addDatagram(2, Command::BRD,  0, &payload, 1);
            int32_t toWrite = frame.finalize();
            std::memcpy(data, frame.data(), toWrite);
            return toWrite;
        }));
    }
    link.processDatagrams();

    ASSERT_EQ(1, process_callback_counter); // datagram lost (invalid frame)
    ASSERT_EQ(2, error_callback_counter);
}


TEST_F(LinkTest, process_datagram_check_timeout_split)
{
    InSequence s;

    int64_t skip{0};
    int64_t logical_read = 0x0001020304050607;
    Command cmd = Command::LRD;
    std::vector<DatagramCheck<int64_t>> expecteds_1(1, {cmd, skip, false}); // no payload for logical read.
    addDatagram(cmd, skip, logical_read, 2, false);
    checkSendFrameRedundancy(expecteds_1); // check frame is sent on both interfaces.

    nanoseconds timeout = 10ms;
    link.setTimeout(timeout);
    EXPECT_CALL(*io_redundancy, setTimeout(timeout));
    io_redundancy->handleReply<int64_t>({logical_read}, 2);
    EXPECT_CALL(*io_nominal, setTimeout(timeout - 1ms)); // Diff is due to since_epoch weak symbol override.
    io_nominal->handleReply<int64_t>({skip}, 0);
    link.processDatagrams();
}


TEST_F(LinkTest, process_datagram_check_timeout_min)
{
    InSequence s;

    int64_t skip{0};
    int64_t logical_read = 0x0001020304050607;
    Command cmd = Command::LRD;
    std::vector<DatagramCheck<int64_t>> expecteds_1(1, {cmd, skip, false}); // no payload for logical read.
    addDatagram(cmd, skip, logical_read, 2, false);
    checkSendFrameRedundancy(expecteds_1); // check frame is sent on both interfaces.

    nanoseconds timeout = -15ms;
    link.setTimeout(timeout);
    EXPECT_CALL(*io_redundancy, setTimeout(timeout));
    io_redundancy->handleReply<int64_t>({logical_read}, 2);
    nanoseconds expected_min_timeout = 0ms;
    EXPECT_CALL(*io_nominal, setTimeout(expected_min_timeout));
    io_nominal->handleReply<int64_t>({skip}, 0);
    link.processDatagrams();
}

TEST_F(LinkTest, event_callback)
{
    InSequence s;

    // Frame context
    int64_t skip{0};
    int64_t logical_read = 0x0001020304050607;
    Command cmd = Command::LRD;
    std::vector<DatagramCheck<int64_t>> expecteds_1(1, {cmd, skip, false}); // no payload for logical read.

    // Attach a callback on a IRQ (trigger on rising edge only)
    bool event_rec = false;
    link.attachEcatEventCallback(EcatEvent::DL_STATUS, [&](){ event_rec = true; });

    // Function to process frames
    auto checkIRQ = [&](bool is_callback_triggered, uint16_t irq)
    {
        event_rec = false;
        addDatagram(cmd, skip, logical_read, 2, false);
        checkSendFrameRedundancy(expecteds_1); // check frame is sent on both interfaces.

        io_redundancy->handleReply<int64_t>({logical_read}, 2, irq);
        io_nominal->handleReply<int64_t>({skip}, 0);

        link.processDatagrams();

        ASSERT_EQ(DatagramState::OK, last_error);
        ASSERT_EQ(is_callback_triggered, event_rec);
    };

    for (int i = 0; i < 5; ++i)
    {
        checkIRQ(false, 0);                         // No IRQ, no trigger
        checkIRQ(true, EcatEvent::DL_STATUS);       // IRQ, trigger (rising edge)
        for (int j = 0; j < 10; ++j)
        {
            checkIRQ(false, EcatEvent::DL_STATUS);  // IRQ, no trigger, mimic a plate
        }
        checkIRQ(false, 0);                         // No IRQ, no trigger (falling edge)
    }
}
}
