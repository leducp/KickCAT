#include <gtest/gtest.h>
#include <cstring>

#include "kickcat/Bus.h"
#include "Mocks.h"

using ::testing::Return;
using ::testing::_;
using ::testing::Invoke;
using ::testing::InSequence;

using namespace kickcat;

class BusTest : public testing::Test
{
public:
    void SetUp() override
    {
        bus.configureWaitLatency(0ns, 0ns);
        init_bus();
    }

    template<typename T>
    void checkSendFrame(Command cmd, T to_check, bool check_payload = true)
    {
        EXPECT_CALL(*io, write(_,_))
        .WillOnce(Invoke([this, cmd, to_check, check_payload](uint8_t const* data, int32_t data_size)
        {
            // store datagram to forge the answer.
            Frame frame(data, data_size);
            inflight = std::move(frame);
            datagram = inflight.data() + sizeof(EthernetHeader) + sizeof(EthercatHeader);
            header = reinterpret_cast<DatagramHeader*>(datagram);
            payload = datagram + sizeof(DatagramHeader);

            if (check_payload)
            {
                EXPECT_EQ(0, std::memcmp(payload, &to_check, sizeof(T)));
            }

            EXPECT_EQ(cmd, header->command);
            return data_size;
        }));
    }

    void checkSendFrame(Command cmd)
    {
        uint8_t skip = 0;
        checkSendFrame(cmd, skip, false);
    }

    template<typename T>
    void handleReply(T answer, uint16_t replied_wkc = 1)
    {
        EXPECT_CALL(*io, read(_,_))
        .WillOnce(Invoke([this, replied_wkc, answer](uint8_t* data, int32_t)
        {
            std::memcpy(payload, &answer, sizeof(T));
            uint16_t* wkc = reinterpret_cast<uint16_t*>(payload + header->len);
            *wkc = replied_wkc;

            while (header->multiple == 1)
            {
                datagram = reinterpret_cast<uint8_t*>(wkc) + 2;
                header = reinterpret_cast<DatagramHeader*>(datagram);
                payload = datagram + sizeof(DatagramHeader);
                wkc = reinterpret_cast<uint16_t*>(payload + header->len);
                *wkc = replied_wkc;
            }

            int32_t answer_size = inflight.finalize();
            std::memcpy(data, inflight.data(), answer_size);
            return answer_size;
        }));
    }

    void handleReply()
    {
        uint8_t no_data = 0;
        handleReply(no_data, 1);
    }

    void addFetchEepromWord(uint32_t word = 0)
    {
        // address
        checkSendFrame(Command::BWR);
        handleReply();

        // eeprom ready
        checkSendFrame(Command::FPRD);
        uint16_t eeprom_ready = 0x0080; // 0x8000 in LE
        handleReply(eeprom_ready);

        // fetch reply
        checkSendFrame(Command::FPRD);
        handleReply(word);
    }

    void init_bus()
    {
        InSequence s;

        // detect slaves
        checkSendFrame(Command::BRD);
        handleReply();

        // reset slaves
        for (int i = 0; i < 9; ++i)
        {
            checkSendFrame(Command::BWR);
            handleReply();
        }

        // set addresses
        checkSendFrame(Command::APWR);
        handleReply();

        // request state: INIT
        checkSendFrame(Command::BWR);
        handleReply();

        // check state
        checkSendFrame(Command::FPRD);
        handleReply(State::INIT);

        // fetch eeprom
        addFetchEepromWord(0xCAFEDECA);     // vendor id
        addFetchEepromWord(0xA5A5A5A5);     // product code
        addFetchEepromWord(0x5A5A5A5A);     // revision number
        addFetchEepromWord(0x12345678);     // serial number
        addFetchEepromWord(0x01001000);     // mailbox rcv offset + size
        addFetchEepromWord(0x02002000);     // mailbox snd offset + size
        addFetchEepromWord(0);              // mailbox protocol: none
        addFetchEepromWord(0);              // eeprom size

        // -- TxPDO
        addFetchEepromWord(0x00080032);     // section TxPDO, 16 bytes
        addFetchEepromWord(0x00010000);     // one entry
        addFetchEepromWord(0);              // 'padding'
        addFetchEepromWord(0x00000000);
        addFetchEepromWord(0x0000FF00);     // 255 bits

        // -- RxPDO
        addFetchEepromWord(0x000C0033);     // section RxPDO, 24 bytes
        addFetchEepromWord(0x00020000);     // two entries
        addFetchEepromWord(0);              // 'padding'
        addFetchEepromWord(0x00000000);
        addFetchEepromWord(0x0000FF00);     // 255 bits
        addFetchEepromWord(0x00000000);
        addFetchEepromWord(0x00008000);     // 128 bits

        // -- SyncManagers
        addFetchEepromWord(0x00080029);     // section SM, 16 bytes
        addFetchEepromWord(0x00000000);     //
        addFetchEepromWord(0x00000000);     //
        addFetchEepromWord(0x00000000);     //
        addFetchEepromWord(0x00000000);     //

        addFetchEepromWord(0xFFFFFFFF);     // end of eeprom

        // configue mailbox:
        // nothing to do

        // request state: PREOP
        checkSendFrame(Command::BWR);
        handleReply();

        // check state
        checkSendFrame(Command::FPRD);
        handleReply(State::PRE_OP);

        // clear mailbox
        // nothing to do

        // add Emergency message: no mailbox here

        bus.init();
        ASSERT_EQ(1, bus.detectedSlaves());

        auto const& slave = bus.slaves().at(0);
        ASSERT_EQ(0xCAFEDECA, slave.vendor_id);
        ASSERT_EQ(0xA5A5A5A5, slave.product_code);
        ASSERT_EQ(0x5A5A5A5A, slave.revision_number);
        ASSERT_EQ(0x12345678, slave.serial_number);
        ASSERT_EQ(0x1000,     slave.mailbox.recv_offset);
        ASSERT_EQ(0x2000,     slave.mailbox.send_offset);
        ASSERT_EQ(0x0100,     slave.mailbox.recv_size);
        ASSERT_EQ(0x0200,     slave.mailbox.send_size);

        ASSERT_EQ(1, slave.sii.TxPDO.size());
        ASSERT_EQ(2, slave.sii.RxPDO.size());
    }

protected:
    std::shared_ptr<MockSocket> io{ std::make_shared<MockSocket>() };
    Bus bus{ io };
    Frame inflight;

    uint8_t* datagram;
    DatagramHeader* header;
    uint8_t* payload;
};

TEST_F(BusTest, nop)
{
    checkSendFrame(Command::NOP);
    handleReply();

    bus.sendNop([](){});
    bus.processAwaitingFrames();
}


TEST_F(BusTest, error_counters)
{
    // refresh errors counters
    ErrorCounters counters{};
    std::memset(&counters, 0, sizeof(ErrorCounters));
    counters.rx[0].invalid_frame = 17;
    counters.rx[0].physical_layer = 34;
    counters.lost_link[0] = 3;

    checkSendFrame(Command::FPRD);
    handleReply(counters);

    bus.sendrefreshErrorCounters([](){});
    bus.processAwaitingFrames();

    auto const& slave = bus.slaves().at(0);
    ASSERT_EQ(34, slave.error_counters.rx[0].physical_layer);
    ASSERT_EQ(17, slave.error_counters.rx[0].invalid_frame);
    ASSERT_EQ(3,  slave.error_counters.lost_link[0]);
}


TEST_F(BusTest, logical_cmd)
{
    InSequence s;

    checkSendFrame(Command::FPWR);
    handleReply();

    uint8_t iomap[64];
    bus.createMapping(iomap);

    // note: bit size is round up
    auto const& slave = bus.slaves().at(0);
    ASSERT_EQ(32,  slave.input.bsize);
    ASSERT_EQ(255, slave.input.size);
    ASSERT_EQ(48,  slave.output.bsize);
    ASSERT_EQ(383, slave.output.size);

    // test logical read/write/read and write

    int64_t logical_read = 0x0001020304050607;
    checkSendFrame(Command::LRD);
    handleReply(logical_read);
    bus.processDataRead([](){});

    for (int i = 0; i < 8; ++i)
    {
        ASSERT_EQ(7 - i, slave.input.data[i]);
    }

    int64_t logical_write = 0x0706050403020100;
    std::memcpy(slave.output.data, &logical_write, sizeof(int64_t));
    checkSendFrame(Command::LWR, logical_write);
    handleReply(logical_write);
    bus.processDataWrite([](){});

    for (int i = 0; i < 8; ++i)
    {
        ASSERT_EQ(7 - i, slave.input.data[i]);
    }

    logical_read  = 0x1011121314151617;
    logical_write = 0x1716151413121110;
    std::memcpy(slave.output.data, &logical_write, sizeof(int64_t));
    checkSendFrame(Command::LRW, logical_write);
    handleReply(logical_read);
    bus.processDataReadWrite([](){});

    for (int i = 0; i < 8; ++i)
    {
        ASSERT_EQ(0x17 - i, slave.input.data[i]);
    }

    // check error callbacks
    uint8_t skip = 0;
    checkSendFrame(Command::LRD);
    handleReply(skip, 0);
    ASSERT_THROW(bus.processDataRead([](){ throw std::out_of_range(""); }), std::out_of_range);

    checkSendFrame(Command::LWR);
    handleReply(skip, 0);
    ASSERT_THROW(bus.processDataWrite([](){ throw std::logic_error(""); }), std::logic_error);

    checkSendFrame(Command::LRW);
    handleReply(skip, 0);
    ASSERT_THROW(bus.processDataReadWrite([](){ throw std::overflow_error(""); }), std::overflow_error);
}


TEST_F(BusTest, AL_status_error)
{
    auto& slave = bus.slaves().at(0);
    struct Feedback
    {
        uint8_t status;
        uint8_t padding[3];
        uint16_t error;

    } __attribute__((__packed__));
    Feedback al{0x11, {}, 0x0020};

    InSequence s;

    checkSendFrame(Command::FPRD);
    handleReply(al);

    try
    {
        bus.getCurrentState(slave);
    }
    catch(ErrorCode const & error)
    {
        ASSERT_EQ(0x0020, error.code());
    }
    ASSERT_EQ(0x11, slave.al_status);

    slave.al_status = State::INVALID;
    checkSendFrame(Command::FPRD);
    handleReply(al, 0);
    bus.getCurrentState(slave);
    ASSERT_EQ(State::INVALID, slave.al_status);

    checkSendFrame(Command::FPRD);
    al.status = 0x1;
    handleReply(al, 1);
    ASSERT_THROW(bus.waitForState(State::OPERATIONAL, 0ns), Error);

    checkSendFrame(Command::BWR);
    uint8_t skip = 0;
    handleReply(skip, 0);
    ASSERT_THROW(bus.requestState(State::INIT), Error);
}


TEST_F(BusTest, messages_errors)
{
    auto& slave = bus.slaves().at(0);
    slave.mailbox.can_read = true;

    uint8_t skip = 0;
    checkSendFrame(Command::FPRD);
    handleReply(skip, 0);
    bus.sendReadMessages([](){ throw std::logic_error(""); });
    ASSERT_THROW(bus.processAwaitingFrames(), std::logic_error);

    checkSendFrame(Command::FPRD);
    handleReply();
    bus.sendReadMessages([](){ throw std::out_of_range(""); });
    ASSERT_THROW(bus.processAwaitingFrames(), std::out_of_range);
}