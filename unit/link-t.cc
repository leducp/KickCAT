#include <gtest/gtest.h>
#include <cstring>

#include "kickcat/Link.h"
#include "Mocks.h"

using ::testing::Return;
using ::testing::_;
using ::testing::Invoke;
using ::testing::InSequence;

using namespace kickcat;

class LinkTest : public testing::Test
{
public:
    void checkSendFrame(int32_t datagrams_number)
    {
        EXPECT_CALL(*io, write(_,_))
        .WillOnce(Invoke([this, datagrams_number](uint8_t const* data, int32_t data_size)
        {
            int32_t available_datagrams = 0;
            Frame frame(data, data_size);
            while (frame.isDatagramAvailable())
            {
                (void)frame.nextDatagram();
                available_datagrams++;
            }
            EXPECT_EQ(datagrams_number, available_datagrams);
            return data_size;
        }));
    }

    template<typename T>
    void addDatagram(T& payload, bool error = false)
    {
        link.addDatagram(Command::BRD, 0, payload,
        [&, error](DatagramHeader const*, uint8_t const*, uint16_t)
        {
            process_callback_counter++;
            if (error)
            {
                return DatagramState::INVALID_WKC;
            }
            return DatagramState::OK;
        },
        [&](DatagramState const& status)
        {
            error_callback_counter++;
            last_error = status;
        });
    }

protected:
    std::shared_ptr<MockSocket> io{ std::make_shared<MockSocket>() };
    Link link{ io };

    int32_t process_callback_counter{0};
    int32_t error_callback_counter{0};
    DatagramState last_error{DatagramState::OK};
};

TEST_F(LinkTest, writeThenRead)
{
    Frame frame;
    EXPECT_CALL(*io, write(_,_))
    .WillOnce(Invoke([&](uint8_t const*, int32_t)
    {
        return ETH_MIN_SIZE;
    }));

    EXPECT_CALL(*io, read(_,_))
    .WillOnce(Invoke([](uint8_t*, int32_t)
    {
        return ETH_MIN_SIZE;
    }));

    link.writeThenRead(frame);
}

TEST_F(LinkTest, writeThenRead_error)
{
    Frame frame;
    EXPECT_CALL(*io, write(_,_))
    .WillOnce(Invoke([&](uint8_t const*, int32_t)
    {
        return 1;
    }));

    ASSERT_THROW(link.writeThenRead(frame), Error);

    EXPECT_CALL(*io, write(_,_))
    .WillOnce(Invoke([&](uint8_t const*, int32_t)
    {
        return ETH_MIN_SIZE;
    }));

    EXPECT_CALL(*io, read(_,_))
    .WillOnce(Invoke([](uint8_t*, int32_t)
    {
        return 1;
    }));

    ASSERT_THROW(link.writeThenRead(frame), Error);
}

TEST_F(LinkTest, add_datagram_more_than_15)
{
    {
        InSequence s;

        checkSendFrame(15);
        checkSendFrame(5);
    }

    uint8_t data;
    for (int32_t i=0; i<20; ++i)
    {
        addDatagram(data);
    }

    link.finalizeDatagrams();

    ASSERT_EQ(0, process_callback_counter);
    ASSERT_EQ(0, error_callback_counter);
}

TEST_F(LinkTest, add_big_datagram)
{
    {
        InSequence s;

        checkSendFrame(5);
        checkSendFrame(1);
    }

    uint8_t data;
    for (int32_t i=0; i<5; ++i)
    {
        addDatagram(data);
    }

    uint8_t big_payload[MAX_ETHERCAT_PAYLOAD_SIZE];
    addDatagram(big_payload);

    link.finalizeDatagrams();
}


TEST_F(LinkTest, add_too_many_datagrams)
{
    constexpr int32_t SEND_DATAGRAMS_OK = 255;
    {
        InSequence s;

        for (int32_t i = 0; i< (SEND_DATAGRAMS_OK / 15); ++i)
        {
            checkSendFrame(15);
        }
    }

    uint8_t data;
    for (int32_t i=0; i<SEND_DATAGRAMS_OK; ++i)
    {
        addDatagram(data);
    }
    EXPECT_THROW(addDatagram(data), Error);
    link.finalizeDatagrams();
}


TEST_F(LinkTest, process_datagrams_nothing_to_do)
{
    link.processDatagrams();
}


TEST_F(LinkTest, process_datagrams_invalid_frame)
{
    checkSendFrame(1);

    uint8_t payload;
    addDatagram(payload);

    EXPECT_CALL(*io, read(_,_))
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
    checkSendFrame(1);

    uint8_t payload;
    addDatagram(payload);

    EXPECT_CALL(*io, read(_,_))
    .WillOnce(Invoke([](uint8_t*, int32_t)
    {
        return 1;
    }));
    link.processDatagrams();

    ASSERT_EQ(0, process_callback_counter);
    ASSERT_EQ(1, error_callback_counter);    // datagram lost (read error)
    ASSERT_EQ(DatagramState::LOST, last_error);
}


TEST_F(LinkTest, process_datagrams_OK)
{
    checkSendFrame(1);

    uint8_t payload;
    addDatagram(payload);

    EXPECT_CALL(*io, read(_,_))
    .WillOnce(Invoke([&](uint8_t*, int32_t)
    {
        return ETH_MIN_SIZE;
    }));
    link.processDatagrams();

    ASSERT_EQ(1, process_callback_counter);
    ASSERT_EQ(0, error_callback_counter);
    ASSERT_EQ(DatagramState::OK, last_error);
}


TEST_F(LinkTest, process_datagrams_send_error)
{
    EXPECT_CALL(*io, write(_,_))
    .WillOnce(Invoke([](uint8_t const*, int32_t)
    {
        return 1;
    }));

    uint8_t payload;
    addDatagram(payload);

    ASSERT_NO_THROW(link.processDatagrams());

    ASSERT_EQ(0, process_callback_counter);
    ASSERT_EQ(1, error_callback_counter);   // datagram lost (sent error)
    ASSERT_EQ(DatagramState::SEND_ERROR, last_error);
}


TEST_F(LinkTest, process_datagrams_error_rethrow)
{
    checkSendFrame(5);

    uint8_t payload;

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

    EXPECT_CALL(*io, read(_,_))
    .WillOnce(Invoke([&](uint8_t*, int32_t)
    {
        return ETH_MIN_SIZE;
    }));
    EXPECT_THROW(link.processDatagrams(), std::overflow_error);
}


TEST_F(LinkTest, process_datagrams_old_frame)
{
    uint8_t payload;

    // first frame - lost
    checkSendFrame(1);
    addDatagram(payload);

    EXPECT_CALL(*io, read(_,_))
    .WillOnce(Invoke([](uint8_t*, int32_t)
    {
        errno = EAGAIN;
        return -1;
    })).RetiresOnSaturation();
    link.processDatagrams();

    ASSERT_EQ(0, process_callback_counter); // datagram lost (invalid frame)
    ASSERT_EQ(1, error_callback_counter);

    // second frame - the previous one that was not THAT lost
    checkSendFrame(1);
    addDatagram(payload);

    {
        InSequence s;

        EXPECT_CALL(*io, read(_,_))
        .WillOnce(Invoke([](uint8_t* data, int32_t)
        {
            Frame frame;
            frame.addDatagram(0, Command::BRD,  0, nullptr, 1);
            int32_t toWrite = frame.finalize();
            std::memcpy(data, frame.data(), toWrite);
            return toWrite;
        })).RetiresOnSaturation();

        EXPECT_CALL(*io, read(_,_))
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
    checkSendFrame(1);
    addDatagram(payload);

    {
        InSequence s;

        EXPECT_CALL(*io, read(_,_))
        .WillOnce(Invoke([](uint8_t* data, int32_t)
        {
            Frame frame;
            frame.addDatagram(1, Command::BRD,  0, nullptr, 1);
            int32_t toWrite = frame.finalize();
            std::memcpy(data, frame.data(), toWrite);
            return toWrite;
        })).RetiresOnSaturation();

        EXPECT_CALL(*io, read(_,_))
        .WillOnce(Invoke([](uint8_t* data, int32_t)
        {
            Frame frame;
            //frame.addDatagram(2, Command::BRD,  0, nullptr, 1);
            frame.addDatagram(2, Command::BRD,  0, nullptr, 1);
            int32_t toWrite = frame.finalize();
            std::memcpy(data, frame.data(), toWrite);
            return toWrite;
        }));
    }
    link.processDatagrams();

    ASSERT_EQ(1, process_callback_counter); // datagram lost (invalid frame)
    ASSERT_EQ(2, error_callback_counter);
}

