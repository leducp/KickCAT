#include "mocks/Sockets.h"

#include "kickcat/Link.h"

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

    // Friendship does not extend to the generated test subclasses: expose the
    // private state needed by the tests here.
    void setIndexes(uint8_t index)
    {
        link.index_head_ = index;
        link.index_queue_ = index;
    }

    DatagramState datagramStatus(uint8_t index)
    {
        return link.callbacks_[index].status;
    }

    uint8_t sentFrames()
    {
        return link.sent_frame_;
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
    .WillOnce(Invoke([&](void const*, int32_t)
    {
        return ETH_MIN_SIZE;
    }));

    EXPECT_CALL(*io_nominal, read(_,_))
    .WillOnce(Invoke([](void*, int32_t)
    {
        return ETH_MIN_SIZE;
    }));

    EXPECT_CALL(*io_nominal, write(_,_))
    .WillOnce(Invoke([&](void const*, int32_t)
    {
        return ETH_MIN_SIZE;
    }));

    EXPECT_CALL(*io_redundancy, read(_,_))
    .WillOnce(Invoke([](void*, int32_t)
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
        .WillOnce(Invoke([&](void const*, int32_t)
        {
            return ETH_MIN_SIZE;
        }));

        EXPECT_CALL(*io_redundancy, read(_,_))
        .WillOnce(Invoke([](void*, int32_t)
        {
            return 0;
        }));

        EXPECT_CALL(*io_nominal, read(_,_))
        .WillOnce(Invoke([](void*, int32_t)
        {
            return 0;
        }));

        // Write into redundancy interface
        EXPECT_CALL(*io_redundancy, write(_,_))
        .WillOnce(Invoke([&](void const*, int32_t)
        {
            return ETH_MIN_SIZE;
        }));

        EXPECT_CALL(*io_nominal, read(_,_))
        .WillOnce(Invoke([](void*, int32_t)
        {
            return 0;
        }));

        EXPECT_CALL(*io_redundancy, read(_,_))
        .WillOnce(Invoke([](void*, int32_t)
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
        .WillOnce(Invoke([&](void const*, int32_t)
        {
            return ETH_MIN_SIZE;
        }));

        EXPECT_CALL(*io_redundancy, read(_,_))
        .WillOnce(Invoke([](void*, int32_t)
        {
            return 0;
        }));

        EXPECT_CALL(*io_nominal, read(_,_))
        .WillOnce(Invoke([](void*, int32_t)
        {
            return ETH_MIN_SIZE;
        }));

        // Write into redundancy interface
        EXPECT_CALL(*io_redundancy, write(_,_))
        .WillOnce(Invoke([&](void const*, int32_t)
        {
            return ETH_MIN_SIZE;
        }));

        EXPECT_CALL(*io_nominal, read(_,_))
        .WillOnce(Invoke([](void*, int32_t)
        {
            return 0;
        }));

        EXPECT_CALL(*io_redundancy, read(_,_))
        .WillOnce(Invoke([](void*, int32_t)
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
        .WillOnce(Invoke([&](void const*, int32_t)
        {
            return ETH_MIN_SIZE;
        }));

        EXPECT_CALL(*io_redundancy, read(_,_))
        .WillOnce(Invoke([](void*, int32_t)
        {
            return 0;
        }));

        EXPECT_CALL(*io_nominal, read(_,_))
        .WillOnce(Invoke([](void*, int32_t)
        {
            return 0;
        }));

        // Write into redundancy interface
        EXPECT_CALL(*io_redundancy, write(_,_))
        .WillOnce(Invoke([&](void const*, int32_t)
        {
            return ETH_MIN_SIZE;
        }));

        EXPECT_CALL(*io_nominal, read(_,_))
        .WillOnce(Invoke([](void*, int32_t)
        {
            return 0;
        }));

        EXPECT_CALL(*io_redundancy, read(_,_))
        .WillOnce(Invoke([](void*, int32_t)
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
    .WillOnce(Invoke([&](void const*, int32_t)
    {
        return ETH_MIN_SIZE;
    }));

    EXPECT_CALL(*io_nominal, read(_,_))
    .WillOnce(Invoke([&](void* frame_in, int32_t)
    {
        EthernetHeader* ethernet_header = pointData<EthernetHeader>(frame_in);
        ethernet_header->type = 0;
        return ETH_MIN_SIZE;
    }));

    EXPECT_CALL(*io_nominal, write(_,_))
    .WillOnce(Invoke([&](void const*, int32_t)
    {
        return ETH_MIN_SIZE;
    }));

    EXPECT_CALL(*io_redundancy, read(_,_))
    .WillOnce(Invoke([&](void* frame_in, int32_t)
    {
        EthernetHeader* ethernet_header = pointData<EthernetHeader>(frame_in);
        ethernet_header->type = 0;
        return ETH_MIN_SIZE;
    }));

    ASSERT_THROW(link.writeThenRead(frame), Error);
}

TEST_F(LinkTest, writeThenRead_error_wrong_number_bytes_read)
{
    Frame frame;
    EXPECT_CALL(*io_redundancy, write(_,_))
    .WillOnce(Invoke([&](void const*, int32_t)
    {
        return ETH_MIN_SIZE;
    }));

    EXPECT_CALL(*io_nominal, read(_,_))
    .WillOnce(Invoke([&](void* frame_in, int32_t)
    {
        EthernetHeader* ethernet_header = pointData<EthernetHeader>(frame_in);
        EthercatHeader* ethercat_header = pointData<EthercatHeader>(ethernet_header);
        ethercat_header->len = ETH_MAX_SIZE;
        return ETH_MAX_SIZE;
    }));

    EXPECT_CALL(*io_nominal, write(_,_))
    .WillOnce(Invoke([&](void const*, int32_t)
    {
        return ETH_MIN_SIZE;
    }));

    EXPECT_CALL(*io_redundancy, read(_,_))
    .WillOnce(Invoke([&](void* frame_in, int32_t)
    {
        EthernetHeader* ethernet_header = pointData<EthernetHeader>(frame_in);
        EthercatHeader* ethercat_header = pointData<EthercatHeader>(ethernet_header);
        ethercat_header->len = ETH_MAX_SIZE;
        return ETH_MAX_SIZE;
    }));

    ASSERT_THROW(link.writeThenRead(frame), Error);
}

TEST_F(LinkTest, writeThenRead_error_write)
{
    Frame frame;
    EXPECT_CALL(*io_nominal, write(_,_))
    .WillOnce(Invoke([&](void const*, int32_t)
    {
        return -1;
    }));
    ASSERT_THROW(link.writeThenRead(frame), Error);
}

TEST_F(LinkTest, isRedundancyNeeded_true)
{
    InSequence s;

    EXPECT_CALL(*io_redundancy, write(_,_))
    .WillOnce(Invoke([&](void const*, int32_t)
    {
        return ETH_MIN_SIZE;
    }));

    EXPECT_CALL(*io_nominal, read(_,_))
    .WillOnce(Invoke([](void*, int32_t)
    {
        // The frame didn't reach the nominal interface.
        return -1;
    }));

    EXPECT_CALL(*io_redundancy, read(_,_))
    .WillOnce(Invoke([](void* frame_in, int32_t)
    {
        // Add a datagram to the read frame and set its working counter to 1.
        Frame frame;
        frame.addDatagram(0, Command::BRD,  0, nullptr, 1);
        int32_t toWrite = frame.finalize();
        std::memcpy(frame_in, frame.data(), toWrite);

        EthernetHeader* ethernet_header = pointData<EthernetHeader>(frame_in);
        EthercatHeader* ethercat_header = pointData<EthercatHeader>(ethernet_header);
        DatagramHeader* datagram_header = pointData<DatagramHeader>(ethercat_header);
        uint8_t* wkc_addr = reinterpret_cast<uint8_t*>(datagram_header) + sizeof(DatagramHeader) + datagram_header->len;

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
    .WillOnce(Invoke([&](void const*, int32_t)
    {
        return ETH_MIN_SIZE;
    }));

    EXPECT_CALL(*io_nominal, read(_,_))
    .WillOnce(Invoke([](void*, int32_t)
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
    .WillOnce(Invoke([&](void const*, int32_t)
    {
        return ETH_MIN_SIZE;
    }));

    EXPECT_CALL(*io_nominal, read(_,_))
    .WillOnce(Invoke([](void*, int32_t)
    {
        return -1;
    }));

    EXPECT_CALL(*io_redundancy, read(_,_))
    .WillOnce(Invoke([](void*, int32_t)
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
    .WillOnce(Invoke([](void const* frame_in, int32_t)
    {
        EthernetHeader const* ethernet_header = pointData<EthernetHeader>(frame_in);
        {
            EXPECT_EQ(ETH_ETHERCAT_TYPE, ethernet_header->type);
            for (int32_t i = 0; i < 6; ++i)
            {
                EXPECT_EQ(PRIMARY_IF_MAC[i], ethernet_header->src[i]);
                EXPECT_EQ(0xff, ethernet_header->dst[i]);
            }
        }

        {
            EthercatHeader const* ethercat_header = pointData<EthercatHeader>(ethernet_header);
            EXPECT_EQ(ethercat_header->len, 0);
        }
        return ETH_MIN_SIZE;
    }));

    EXPECT_CALL(*io_redundancy, write(_,_))
    .WillOnce(Invoke([](void const* frame_in, int32_t)
    {
        EthernetHeader const* ethernet_header = pointData<EthernetHeader>(frame_in);
        {
            EXPECT_EQ(ETH_ETHERCAT_TYPE, ethernet_header->type);
            for (int32_t i = 0; i < 6; ++i)
            {
                EXPECT_EQ(SECONDARY_IF_MAC[i], ethernet_header->src[i]);
                EXPECT_EQ(0xff, ethernet_header->dst[i]);
            }
        }

        {
            EthercatHeader const* ethercat_header = pointData<EthercatHeader>(ethernet_header);
            EXPECT_EQ(ethercat_header->len, 0);
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


TEST_F(LinkTest, process_datagrams_lrw_in_mapping_splices_both_copies)
{
    InSequence s;

    // Split ring: slave 0's input (bytes 0-3) is in the prefix copy, slave 1's (bytes 4-7)
    // in the suffix copy. Only the description-driven merge can rebuild the full payload.
    int64_t sent      = 0x00000000FFFFFFFF;
    int64_t tail_copy = 0x0102030405060708; // suffix: slave 1 input valid (bytes 4-7, LE)
    int64_t head_copy = 0x1112131415161718; // prefix: slave 0 input valid (bytes 0-3, LE)
    int64_t merged    = 0x0102030415161718;

    LogicalFrameDescription desc{};
    desc.address = 0;
    desc.logical_size = sizeof(sent);
    desc.pdo_size = sizeof(sent);
    desc.entries = {{3, 0, 4}, {3, 4, 4}};
    link.setLogicalMapping({desc});

    std::vector<DatagramCheck<int64_t>> expecteds_1(1, {Command::LRW, sent});
    addDatagram(Command::LRW, sent, merged, 6, false);
    checkSendFrameRedundancy(expecteds_1);

    io_redundancy->handleReply<int64_t>({tail_copy}, 3); // lands in the nominal frame
    io_nominal->handleReply<int64_t>({head_copy}, 3);    // lands in the redundancy frame

    link.processDatagrams();

    ASSERT_EQ(1, process_callback_counter);
    ASSERT_EQ(0, error_callback_counter);
    ASSERT_EQ(DatagramState::OK, last_error);
}


TEST_F(LinkTest, process_datagrams_lrw_keeps_processed_copy)
{
    InSequence s;

    // Inputs and outputs share logical addresses: the unprocessed ring copy echoes the
    // output payload where the processed one carries inputs. OR-ing them would corrupt
    // the inputs - the merge must keep the processed (nominal) copy.
    int64_t output_payload = 0x00000000FFFFFFFF;
    int64_t processed      = 0x0102030405060708;
    Command cmd = Command::LRW;
    std::vector<DatagramCheck<int64_t>> expecteds_1(1, {cmd, output_payload});
    addDatagram(cmd, output_payload, processed, 3, false);
    checkSendFrameRedundancy(expecteds_1);

    io_redundancy->handleReply<int64_t>({processed}, 3);   // copy processed by the slaves
    io_nominal->handleReply<int64_t>({output_payload}, 0); // unprocessed echo

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
    .WillOnce(Invoke([](void* data, int32_t)
    {
        std::memset(data, 0, ETH_MIN_SIZE);
        return ETH_MIN_SIZE;
    }));

    EXPECT_CALL(*io_redundancy, read(_,_))
    .WillOnce(Invoke([](void* data, int32_t)
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
    .WillOnce(Invoke([](void*, int32_t)
    {
        return 1;
    }));

    EXPECT_CALL(*io_redundancy, read(_,_))
    .WillOnce(Invoke([](void*, int32_t)
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
    .WillOnce(Invoke([](void const*, int32_t)
    {
        return 1;
    }));

    EXPECT_CALL(*io_redundancy, write(_,_))
    .WillOnce(Invoke([](void const*, int32_t)
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
    .WillOnce(Invoke([&](void*, int32_t)
    {
        return ETH_MIN_SIZE;
    }));
    EXPECT_CALL(*io_redundancy, read(_,_))
    .WillOnce(Invoke([&](void*, int32_t)
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
    .WillOnce(Invoke([](void*, int32_t)
    {
        errno = EAGAIN;
        return -1;
    })).RetiresOnSaturation();

    EXPECT_CALL(*io_redundancy, read(_,_))
    .WillOnce(Invoke([](void*, int32_t)
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
        .WillOnce(Invoke([](void*, int32_t)
        {
            return -1;
        })).RetiresOnSaturation();

        EXPECT_CALL(*io_nominal, read(_,_))
        .WillOnce(Invoke([](void* data, int32_t)
        {
            Frame frame;
            frame.addDatagram(0, Command::BRD,  0, nullptr, 1);
            int32_t toWrite = frame.finalize();
            std::memcpy(data, frame.data(), toWrite);
            return toWrite;
        })).RetiresOnSaturation();


        EXPECT_CALL(*io_redundancy, read(_,_))
        .WillOnce(Invoke([](void*, int32_t)
        {
            return -1;
        })).RetiresOnSaturation();

        EXPECT_CALL(*io_nominal, read(_,_))
        .WillOnce(Invoke([](void*, int32_t)
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
        .WillOnce(Invoke([](void*, int32_t)
        {
            return -1;
        })).RetiresOnSaturation();

        EXPECT_CALL(*io_nominal, read(_,_))
        .WillOnce(Invoke([](void* data, int32_t)
        {
            Frame frame;
            frame.addDatagram(1, Command::BRD,  0, nullptr, 1);
            int32_t toWrite = frame.finalize();
            std::memcpy(data, frame.data(), toWrite);
            return toWrite;
        })).RetiresOnSaturation();

        // handle only response on nominal
        EXPECT_CALL(*io_redundancy, read(_,_))
        .WillOnce(Invoke([](void*, int32_t)
        {
            return -1;
        })).RetiresOnSaturation();

        EXPECT_CALL(*io_nominal, read(_,_))
        .WillOnce(Invoke([payload](void* data, int32_t)
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
    EXPECT_CALL(*io_nominal, setTimeout(timeout - 1ms)); // Diff is due to the mocked clock override.
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
    checkIRQ(false, EcatEvent::DC_LATCH);           // call default callback -> test that nothing crash
}


TEST_F(LinkTest, event_callback_empty_is_rejected)
{
    // checkEcatEvents() invokes callbacks unchecked from the cyclic path: an empty
    // std::function is a caller bug, refused at configuration time.
    ASSERT_THROW(link.attachEcatEventCallback(EcatEvent::DL_STATUS, {}), Error);
}


TEST_F(LinkTest, process_datagrams_stale_frame_on_nominal_socket_keeps_current_cycle)
{
    InSequence s;

    int64_t skip{0};
    int64_t stale_data   = 0x00000000DEADBEEF;
    int64_t current_data = 0x0001020304050607;
    Command cmd = Command::LRD;
    std::vector<DatagramCheck<int64_t>> expecteds_1(1, {cmd, skip, false}); // no payload for logical read.

    // Cycle 1: sent on the nominal interface only, no response - reported lost. The mock
    // keeps the cycle-1 frame queued on io_nominal to replay it as a stale response later.
    addDatagram(cmd, skip, current_data, 2, false);
    io_nominal->checkSendFrame(expecteds_1);
    EXPECT_CALL(*io_redundancy, write(_,_)).WillOnce(Return(0));
    io_redundancy->readError();
    io_nominal->readError();

    link.processDatagrams();

    ASSERT_EQ(0, process_callback_counter);
    ASSERT_EQ(1, error_callback_counter);
    ASSERT_EQ(DatagramState::LOST, last_error);

    // Cycle 2: the redundancy socket delivers the current response while the nominal
    // socket delivers the stale cycle-1 one.
    addDatagram(cmd, skip, current_data, 2, false);
    checkSendFrameRedundancy(expecteds_1);
    io_redundancy->handleReply<int64_t>({current_data}, 2); // current frame
    io_nominal->handleReply<int64_t>({stale_data}, 1);      // stale cycle-1 frame (index 0)
    io_redundancy->readError();                             // retry read granted by the dropped datagram
    io_nominal->readError();

    link.processDatagrams();

    // Current datagram dispatched with the current data, stale one silently dropped.
    ASSERT_EQ(1, process_callback_counter);
    ASSERT_EQ(1, error_callback_counter);
}


TEST_F(LinkTest, process_datagrams_stale_pair_is_dropped)
{
    InSequence s;

    int64_t sent         = 0x00000000FFFFFFFF;
    int64_t stale_data   = 0x00000000DEADBEEF;
    int64_t current_data = 0x0102030405060708;

    std::vector<DatagramCheck<int64_t>> expecteds_1(1, {Command::LRW, sent});

    LogicalFrameDescription desc{};
    desc.address = 0;
    desc.logical_size = sizeof(sent);
    desc.pdo_size = sizeof(sent);
    desc.entries = {{3, 0, 8}};
    link.setLogicalMapping({desc});

    auto queueLRW = [&]()
    {
        link.addDatagram(Command::LRW, 0, &sent, sizeof(sent),
            [&](DatagramHeader const*, uint8_t const* data, uint16_t wkc)
            {
                process_callback_counter++;
                EXPECT_EQ(3, wkc); // sum of both current copies
                EXPECT_EQ(0, std::memcmp(data, &current_data, sizeof(current_data)));
                return DatagramState::OK;
            },
            [&](DatagramState const& status)
            {
                error_callback_counter++;
                last_error = status;
            });
    };

    // Cycle 1: no response on either interface - reported lost, both mocks keep the
    // cycle-1 frame queued.
    queueLRW();
    checkSendFrameRedundancy(expecteds_1);
    io_redundancy->readError();
    io_nominal->readError();

    link.processDatagrams();

    ASSERT_EQ(0, process_callback_counter);
    ASSERT_EQ(1, error_callback_counter);
    ASSERT_EQ(DatagramState::LOST, last_error);
    last_error = DatagramState::OK;

    // Cycle 2: both interfaces deliver the stale cycle-1 pair first, the current pair behind.
    queueLRW();
    checkSendFrameRedundancy(expecteds_1);
    io_redundancy->handleReply<int64_t>({stale_data}, 1);
    io_nominal->handleReply<int64_t>({stale_data}, 1);
    io_redundancy->handleReply<int64_t>({current_data}, 2);
    io_nominal->handleReply<int64_t>({current_data}, 1);

    link.processDatagrams();

    // The stale pair was dropped by the in-flight window: only the current pair was dispatched.
    ASSERT_EQ(1, process_callback_counter);
    ASSERT_EQ(1, error_callback_counter);
    ASSERT_EQ(DatagramState::OK, last_error);
}


TEST_F(LinkTest, process_datagrams_mismatched_pair_dispatches_both_datagrams)
{
    InSequence s;

    int64_t skip{0};
    int64_t logical_read = 1111;
    Command cmd = Command::LRD;
    std::vector<DatagramCheck<int64_t>> expecteds_15(15, {cmd, skip, false}); // no payload for logical read.
    std::vector<DatagramCheck<int64_t>> expecteds_4(4, {cmd, skip, false});
    std::vector<int64_t> answers_15(15, logical_read);
    std::vector<int64_t> answers_4(4, logical_read);
    std::vector<int64_t> skips_4(4, skip);

    // Frame 1 is lost on the nominal interface at send time: only the redundancy
    // socket will ever deliver it.
    EXPECT_CALL(*io_nominal, write(_,_)).WillOnce(Return(0));
    io_redundancy->checkSendFrame(expecteds_15);
    io_nominal->checkSendFrame(expecteds_4);
    io_redundancy->checkSendFrame(expecteds_4);

    for (int32_t j = 0; j < 19; j++)
    {
        addDatagram(cmd, skip, logical_read, 2, false);
    }

    // First read round pairs frame 1 (from io_redundancy) with frame 2 (from io_nominal):
    // mismatched indexes, frame 1 datagrams dispatched, frame 2 copies dropped.
    io_redundancy->handleReply<int64_t>(answers_15, 2);
    io_nominal->handleReply<int64_t>(skips_4, 0);
    // Retry read granted by the dropped datagrams: frame 2 recovered from io_redundancy.
    io_redundancy->handleReply<int64_t>(answers_4, 2);
    io_nominal->readError();
    io_redundancy->readError();
    io_nominal->readError();

    link.processDatagrams();

    ASSERT_EQ(19, process_callback_counter);
    ASSERT_EQ(0, error_callback_counter);
    ASSERT_EQ(DatagramState::OK, last_error);
}


TEST_F(LinkTest, process_datagrams_merged_pair_aggregates_irq_of_both_copies)
{
    InSequence s;

    int64_t skip{0};
    int64_t logical_read = 0x0001020304050607;
    Command cmd = Command::LRD;
    std::vector<DatagramCheck<int64_t>> expecteds_1(1, {cmd, skip, false}); // no payload for logical read.

    bool dl_status_event = false;
    bool sm0_status_event = false;
    link.attachEcatEventCallback(EcatEvent::DL_STATUS,  [&](){ dl_status_event = true; });
    link.attachEcatEventCallback(EcatEvent::SM0_STATUS, [&](){ sm0_status_event = true; });

    addDatagram(cmd, skip, logical_read, 2, false);
    checkSendFrameRedundancy(expecteds_1); // check frame is sent on both interfaces.

    // Each ring segment reports its own events: the merged pair must carry both.
    io_redundancy->handleReply<int64_t>({logical_read}, 2, EcatEvent::DL_STATUS);
    io_nominal->handleReply<int64_t>({skip}, 0, EcatEvent::SM0_STATUS);

    link.processDatagrams();

    ASSERT_EQ(1, process_callback_counter);
    ASSERT_EQ(0, error_callback_counter);
    ASSERT_EQ(DatagramState::OK, last_error);
    ASSERT_TRUE(dl_status_event);
    ASSERT_TRUE(sm0_status_event);
}


TEST_F(LinkTest, process_datagrams_never_downgrades_ok_status)
{
    uint8_t data = 3;
    Command cmd = Command::BWR;
    std::vector<DatagramCheck<uint8_t>> expecteds_small(1, {cmd, data});
    std::vector<uint8_t> answers_small(1, data);

    std::array<uint8_t, MAX_ETHERCAT_PAYLOAD_SIZE> big_payload;
    std::array<uint8_t, MAX_ETHERCAT_PAYLOAD_SIZE> big_garbage;
    std::fill(std::begin(big_payload), std::end(big_payload), 2);
    std::fill(std::begin(big_garbage), std::end(big_garbage), 0xAA);

    std::vector<DatagramCheck<std::array<uint8_t, MAX_ETHERCAT_PAYLOAD_SIZE>>> expecteds_big(1, {cmd, big_payload});
    std::vector<std::array<uint8_t, MAX_ETHERCAT_PAYLOAD_SIZE>> answers_big(1, big_payload);
    std::vector<std::array<uint8_t, MAX_ETHERCAT_PAYLOAD_SIZE>> answers_garbage(1, big_garbage);

    InSequence s;

    checkSendFrameRedundancy(expecteds_big);   // frame 1: index 0 alone (fills the frame)
    checkSendFrameRedundancy(expecteds_small); // frame 2: index 1 alone

    addDatagram(cmd, big_payload, big_payload, 2);
    addDatagram(cmd, data, data, 2);

    // Round 1: index 0 dispatched OK from the redundancy socket alone; the nominal
    // socket keeps its copy queued.
    io_redundancy->handleReply<std::array<uint8_t, MAX_ETHERCAT_PAYLOAD_SIZE>>(answers_big, 2);
    io_nominal->readError();
    // Round 2: the nominal socket delivers the index-0 duplicate (garbage payload):
    // already OK, it must be skipped, and the dropped index-1 copy grants a retry.
    io_redundancy->handleReply<uint8_t>(answers_small, 2);
    io_nominal->handleReply<std::array<uint8_t, MAX_ETHERCAT_PAYLOAD_SIZE>>(answers_garbage, 0);
    // Round 3: index 1 recovered from the nominal socket.
    io_redundancy->readError();
    io_nominal->handleReply<uint8_t>(answers_small, 2);

    link.processDatagrams();

    ASSERT_EQ(2, process_callback_counter); // duplicate dispatch skipped
    ASSERT_EQ(0, error_callback_counter);
    ASSERT_EQ(DatagramState::OK, last_error);
    ASSERT_EQ(DatagramState::OK, datagramStatus(0));
    ASSERT_EQ(DatagramState::OK, datagramStatus(1));
}


TEST_F(LinkTest, send_error_marks_wrapped_indexes)
{
    // Start near the index wrap so the frame spans indexes 254, 255, 0, 1.
    setIndexes(254);
    for (int32_t i = 0; i < 4; ++i)
    {
        link.addDatagram(Command::BRD, createAddress(0, 0x0000), nullptr, nullptr, nullptr);
    }

    EXPECT_CALL(*io_nominal, write(_,_)).WillOnce(Return(-1));
    EXPECT_CALL(*io_redundancy, write(_,_)).WillOnce(Return(-1));

    link.finalizeDatagrams();

    ASSERT_EQ(0, sentFrames());
    ASSERT_EQ(DatagramState::SEND_ERROR, datagramStatus(254));
    ASSERT_EQ(DatagramState::SEND_ERROR, datagramStatus(255));
    ASSERT_EQ(DatagramState::SEND_ERROR, datagramStatus(0));
    ASSERT_EQ(DatagramState::SEND_ERROR, datagramStatus(1));
}


TEST_F(LinkTest, process_datagrams_split_read_or_merge_per_command)
{
    InSequence s;

    int64_t skip{0};
    int64_t half_head = 0x0001020300000000;
    int64_t half_tail = 0x0000000004050607;
    int64_t full      = 0x0001020304050607;

    // LRD is covered elsewhere: pin the other OR-merged read commands. BRW reads
    // OR-accumulate over the echoed payload (zero here), so it merges the same way.
    std::vector<Command> commands{Command::BRD, Command::APRD, Command::FPRD, Command::BRW};
    for (auto cmd : commands)
    {
        std::vector<DatagramCheck<int64_t>> expecteds_1(1, {cmd, skip, false}); // payload zeroed for read commands.
        addDatagram(cmd, skip, full, 2, false);
        checkSendFrameRedundancy(expecteds_1); // check frame is sent on both interfaces.

        // Line cut between slaves: each socket returns half the payload.
        io_redundancy->handleReply<int64_t>({half_tail}, 1);
        io_nominal->handleReply<int64_t>({half_head}, 1);

        link.processDatagrams();
    }

    ASSERT_EQ(4, process_callback_counter);
    ASSERT_EQ(0, error_callback_counter);
    ASSERT_EQ(DatagramState::OK, last_error);
}


TEST_F(LinkTest, process_datagrams_single_responder_rw_takes_answered_copy)
{
    InSequence s;

    // RW command addressed to a slave on the head segment of a split ring: the read data
    // is only in the head copy, the tail copy streams the echoed request through unprocessed.
    int64_t sent      = 0x00000000FFFFFFFF;
    int64_t read_data = 0x0102030405060708;
    std::vector<Command> commands{Command::FPRW, Command::APRW};
    for (auto cmd : commands)
    {
        std::vector<DatagramCheck<int64_t>> expecteds_1(1, {cmd, sent});
        addDatagram(cmd, sent, read_data, 3, false);
        checkSendFrameRedundancy(expecteds_1);

        io_redundancy->handleReply<int64_t>({sent}, 0);    // tail copy: echo, lands in the nominal frame
        io_nominal->handleReply<int64_t>({read_data}, 3);  // head copy: slave answered

        link.processDatagrams();
    }

    ASSERT_EQ(2, process_callback_counter);
    ASSERT_EQ(0, error_callback_counter);
    ASSERT_EQ(DatagramState::OK, last_error);
}


TEST_F(LinkTest, lrw_mapping_dispatched_by_logical_address)
{
    InSequence s;

    // Two mapped logical frames with different layouts: each LRW pair must be spliced
    // with the description matching its own logical address.
    int64_t sent = 0x00000000FFFFFFFF;

    LogicalFrameDescription desc0{};
    desc0.address = 0;
    desc0.logical_size = sizeof(sent);
    desc0.pdo_size = sizeof(sent);
    desc0.entries = {{3, 0, 4}, {3, 4, 4}}; // two slaves: prefix covers bytes 0-3 only
    LogicalFrameDescription desc1{};
    desc1.address = 0x800;
    desc1.logical_size = sizeof(sent);
    desc1.pdo_size = sizeof(sent);
    desc1.entries = {{3, 0, 8}};            // one slave: prefix covers the full payload
    link.setLogicalMapping({desc0, desc1});

    int64_t tail0   = 0x0102030405060708;
    int64_t head0   = 0x1112131415161718;
    int64_t merged0 = 0x0102030415161718; // bytes 0-3 from the prefix copy
    int64_t tail1   = 0x2122232425262728;
    int64_t head1   = 0x3132333435363738;
    int64_t merged1 = head1;              // full payload from the prefix copy

    int32_t checked = 0;
    auto queueLRW = [&](uint32_t address, int64_t expected)
    {
        link.addDatagram(Command::LRW, address, &sent, sizeof(sent),
            [&checked, expected](DatagramHeader const*, uint8_t const* data, uint16_t wkc)
            {
                ++checked;
                EXPECT_EQ(6, wkc);
                EXPECT_EQ(0, std::memcmp(data, &expected, sizeof(expected)));
                return DatagramState::OK;
            },
            [&](DatagramState const& status)
            {
                error_callback_counter++;
                last_error = status;
            });
    };

    std::vector<DatagramCheck<int64_t>> expecteds_2(2, {Command::LRW, sent});
    queueLRW(0, merged0);
    queueLRW(0x800, merged1);
    checkSendFrameRedundancy(expecteds_2);

    io_redundancy->handleReply<int64_t>({tail0, tail1}, 3); // suffix copies, nominal frame
    io_nominal->handleReply<int64_t>({head0, head1}, 3);    // prefix copies, redundancy frame

    link.processDatagrams();

    ASSERT_EQ(2, checked);
    ASSERT_EQ(0, error_callback_counter);
    ASSERT_EQ(DatagramState::OK, last_error);
}


TEST_F(LinkTest, process_datagrams_lrw_outside_mapping_takes_covering_copy)
{
    InSequence s;

    // No logical mapping provided and the processed copy landed on the redundancy side:
    // the fallback must take the copy that covered the slaves, not the echoed one.
    int64_t output_payload = 0x00000000FFFFFFFF;
    int64_t processed      = 0x0102030405060708;
    Command cmd = Command::LRW;
    std::vector<DatagramCheck<int64_t>> expecteds_1(1, {cmd, output_payload});
    addDatagram(cmd, output_payload, processed, 3, false);
    checkSendFrameRedundancy(expecteds_1);

    io_redundancy->handleReply<int64_t>({output_payload}, 0); // unprocessed echo
    io_nominal->handleReply<int64_t>({processed}, 3);         // copy processed by the slaves

    link.processDatagrams();

    ASSERT_EQ(1, process_callback_counter);
    ASSERT_EQ(0, error_callback_counter);
    ASSERT_EQ(DatagramState::OK, last_error);
}


TEST_F(LinkTest, lrw_not_matching_mapping_layout_uses_single_segment_rule)
{
    InSequence s;

    // An LRW at a mapped address but with a different length is not the mapped datagram:
    // splicing it with the mapping layout would write logical_size bytes over a smaller
    // payload. It must take the single-segment rule instead.
    int64_t sent      = 0x00000000FFFFFFFF;
    int64_t tail_copy = 0x0102030405060708;
    int64_t head_copy = 0x1112131415161718;

    LogicalFrameDescription desc{};
    desc.address = 0;
    desc.logical_size = 16; // twice the datagram length below
    desc.pdo_size = 16;
    desc.entries = {{3, 0, 4}, {3, 4, 4}};
    link.setLogicalMapping({desc});

    std::vector<DatagramCheck<int64_t>> expecteds_1(1, {Command::LRW, sent});
    addDatagram(Command::LRW, sent, tail_copy, 6, false); // nominal copy kept untouched
    checkSendFrameRedundancy(expecteds_1);

    io_redundancy->handleReply<int64_t>({tail_copy}, 3);
    io_nominal->handleReply<int64_t>({head_copy}, 3);

    link.processDatagrams();

    ASSERT_EQ(1, process_callback_counter);
    ASSERT_EQ(0, error_callback_counter);
    ASSERT_EQ(DatagramState::OK, last_error);
}


// Hand-built 3-slave frame: inputs of 4 bytes at offsets 0/8/16, each slave contributing 3 (in+out).
// Buffer roles follow Link::read()'s socket crossover: on a split ring `nominal` holds the
// tail-injected copy (bus-order suffix), `redundancy` the head-injected copy (bus-order prefix).
class LRWMergeTest : public testing::Test
{
public:
    void SetUp() override
    {
        desc.logical_size = 26;
        desc.pdo_size = 24; // last 2 bytes emulate a mailbox status area
        for (int i = 0; i < 3; ++i)
        {
            desc.entries.push_back({3, i * 8, 4});
        }

        for (size_t i = 0; i < sizeof(nominal); ++i)
        {
            nominal[i] = static_cast<uint8_t>(0x10 + i);
            redundancy[i] = static_cast<uint8_t>(0xA0 + i);
        }
    }

protected:
    LogicalFrameDescription desc{};
    uint8_t nominal[26];
    uint8_t redundancy[26];
};


TEST_F(LRWMergeTest, intact_ring_keeps_nominal_copy)
{
    // Intact ring: the head-injected copy processed every slave and exits at the tail, so it
    // lands in the nominal buffer; the tail-injected copy streams through unprocessed (wkc 0).
    uint8_t const expected[26] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x1B, 0x1C,
                                  0x1D, 0x1E, 0x1F, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29};
    mergeSplitLRW(desc, nominal, redundancy, 9, 0);
    ASSERT_EQ(0, std::memcmp(nominal, expected, sizeof(expected)));
}


TEST_F(LRWMergeTest, break_on_last_cable_takes_head_copy)
{
    // Break between the last slave and the tail master port: the tail-injected copy loops back
    // before reaching any slave (wkc 0), the head-injected copy processed everyone.
    mergeSplitLRW(desc, nominal, redundancy, 0, 9);
    ASSERT_EQ(0, std::memcmp(nominal, redundancy, sizeof(redundancy)));
}


TEST_F(LRWMergeTest, split_after_slave0_takes_head_input_from_prefix_copy)
{
    // Break between slave 0 and slave 1: the head copy processed slave 0 (wkc 3, prefix ->
    // redundancy buffer), the tail copy processed slaves 1 and 2 (wkc 6 -> nominal buffer).
    mergeSplitLRW(desc, nominal, redundancy, 6, 3);

    uint8_t const expected[26] = {0xA0, 0xA1, 0xA2, 0xA3,  // slave 0 input: prefix (redundancy)
                                  0x14, 0x15, 0x16, 0x17,  // gap (outputs): nominal
                                  0x18, 0x19, 0x1A, 0x1B,  // slave 1 input: suffix (nominal)
                                  0x1C, 0x1D, 0x1E, 0x1F,
                                  0x20, 0x21, 0x22, 0x23,  // slave 2 input: suffix (nominal)
                                  0x24, 0x25, 0x26, 0x27,
                                  0x28 | 0xB8, 0x29 | 0xB9}; // status area: OR of both copies
    ASSERT_EQ(0, std::memcmp(nominal, expected, sizeof(expected)));
}


TEST_F(LRWMergeTest, split_before_last_slave)
{
    // Break between slave 1 and slave 2: the head copy processed slaves 0 and 1 (wkc 6,
    // prefix -> redundancy buffer), the tail copy processed slave 2 only (wkc 3).
    mergeSplitLRW(desc, nominal, redundancy, 3, 6);

    uint8_t const expected[26] = {0xA0, 0xA1, 0xA2, 0xA3,  // slave 0 input: prefix (redundancy)
                                  0x14, 0x15, 0x16, 0x17,
                                  0xA8, 0xA9, 0xAA, 0xAB,  // slave 1 input: prefix (redundancy)
                                  0x1C, 0x1D, 0x1E, 0x1F,
                                  0x20, 0x21, 0x22, 0x23,  // slave 2 input: suffix (nominal)
                                  0x24, 0x25, 0x26, 0x27,
                                  0x28 | 0xB8, 0x29 | 0xB9};
    ASSERT_EQ(0, std::memcmp(nominal, expected, sizeof(expected)));
}


TEST_F(LRWMergeTest, wkc_inside_a_contribution_attributes_started_slave_to_prefix)
{
    // wkc 4 lands inside slave 1's contribution of 3 (partial answer: the summed-wkc check
    // discards the cycle anyway): the attribution must keep copying until the count is
    // covered, so slave 1 is taken from the prefix copy like slave 0.
    mergeSplitLRW(desc, nominal, redundancy, 5, 4);

    uint8_t const expected[26] = {0xA0, 0xA1, 0xA2, 0xA3,  // slave 0 input: prefix (redundancy)
                                  0x14, 0x15, 0x16, 0x17,
                                  0xA8, 0xA9, 0xAA, 0xAB,  // slave 1 input: prefix (redundancy)
                                  0x1C, 0x1D, 0x1E, 0x1F,
                                  0x20, 0x21, 0x22, 0x23,  // slave 2 input: suffix (nominal)
                                  0x24, 0x25, 0x26, 0x27,
                                  0x28 | 0xB8, 0x29 | 0xB9};
    ASSERT_EQ(0, std::memcmp(nominal, expected, sizeof(expected)));
}
}
