#include <gtest/gtest.h>
#include <cstring>

#include "kickcat/Bus.h"
#include "kickcat/LinkSingle.h"
#include "Mocks.h"

using ::testing::Return;
using ::testing::_;
using ::testing::Invoke;
using ::testing::InSequence;

using namespace kickcat;

struct SDOAnswer
{
    mailbox::Header header;
    mailbox::ServiceData sdo;
    uint8_t payload[4];
} __attribute__((__packed__));

class BusTest : public testing::Test
{
public:
    void SetUp() override
    {
        bus.configureWaitLatency(0ns, 0ns);
        initBus();
    }


    void checkSendFrameSimple(Command cmd)
    {
        uint8_t skip = 0;
        io->checkSendFrame(cmd, skip, false);
    }


    void handleReplySimple(uint16_t wkc = 1)
    {
        io->handleReply<uint8_t>({0}, wkc);
    }

    void addFetchEepromWord(uint32_t word)
    {
        // address
        checkSendFrameSimple(Command::BWR);
        handleReplySimple();

        // eeprom ready
        checkSendFrameSimple(Command::FPRD);
        io->handleReply<uint16_t>({0x0080}); // 0x8000 in LE

        // fetch reply
        checkSendFrameSimple(Command::FPRD);
        io->handleReply<uint32_t>({word});
    }

    void detectAndReset()
    {
        InSequence s;

        // detect slaves
        checkSendFrameSimple(Command::BRD);
        handleReplySimple();

        // reset slaves
        for (int i = 0; i < 8; ++i)
        {
            checkSendFrameSimple(Command::BWR);
            handleReplySimple();
        }
    }

    void initBus(milliseconds watchdog = 100ms)
    {
        InSequence s;

        detectAndReset();

        // PDIO Watchdog
        uint16_t watchdogTimeCheck = static_cast<uint16_t>(watchdog / 100us);
        io->checkSendFrame(Command::BWR, uint16_t(0x09C2), true);
        handleReplySimple();
        io->checkSendFrame(Command::BWR, watchdogTimeCheck, true);
        handleReplySimple();
        io->checkSendFrame(Command::BWR, watchdogTimeCheck, true);
        handleReplySimple();

        // eeprom to master
        checkSendFrameSimple(Command::BWR);
        handleReplySimple();

        // set addresses
        checkSendFrameSimple(Command::APWR);
        handleReplySimple();

        // request state: INIT
        checkSendFrameSimple(Command::BWR);
        handleReplySimple();

        // check state
        checkSendFrameSimple(Command::FPRD);
        io->handleReply<uint8_t>({State::INIT});

        // fetch eeprom
        addFetchEepromWord(0xCAFEDECA);     // vendor id
        addFetchEepromWord(0xA5A5A5A5);     // product code
        addFetchEepromWord(0x5A5A5A5A);     // revision number
        addFetchEepromWord(0x12345678);     // serial number
        addFetchEepromWord(0x01001000);     // mailbox rcv offset + size
        addFetchEepromWord(0x02002000);     // mailbox snd offset + size
        addFetchEepromWord(4);              // mailbox protocol: CoE
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
        checkSendFrameSimple(Command::FPWR);
        handleReplySimple();

        // request state: PREOP
        checkSendFrameSimple(Command::BWR);
        handleReplySimple();

        // check state
        checkSendFrameSimple(Command::FPRD);
        io->handleReply<uint8_t>({State::PRE_OP});

        // clear mailbox
        checkSendFrameSimple(Command::FPRD);
        io->handleReply<uint8_t>({0x08, 0}); // can write, nothing to read

        bus.init(watchdog);

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

    template<typename T>
    void addReadEmulatedSDO(uint16_t index, std::vector<T> const& data_to_reply)
    {
        InSequence s;

        SDOAnswer answer;
        answer.header.len = 10;
        answer.header.type = mailbox::Type::CoE;
        answer.sdo.service = CoE::Service::SDO_RESPONSE;
        answer.sdo.command = CoE::SDO::response::UPLOAD;
        answer.sdo.index = index;
        answer.sdo.transfer_type = 1;
        answer.sdo.block_size = 4 - sizeof(T);
        std::memset(answer.payload, 0, 4);

        for (uint32_t i = 0; i < data_to_reply.size(); ++i)
        {
            checkSendFrameSimple(Command::FPRD);
            io->handleReply<uint8_t>({0, 0});// can write, nothing to read

            checkSendFrameSimple(Command::FPWR);  // write to mailbox
            handleReplySimple();

            checkSendFrameSimple(Command::FPRD);
            io->handleReply<uint8_t>({0, 0x08});// can write, something to read

            answer.sdo.subindex = static_cast<uint8_t>(i);
            std::memcpy(answer.payload, &data_to_reply[i], sizeof(T));
            checkSendFrameSimple(Command::FPRD);
            io->handleReply<SDOAnswer>({answer}); // read answer
        }
    }

protected:
    std::shared_ptr<MockSocket> io{ std::make_shared<MockSocket>() };
    std::shared_ptr<LinkSingle> link = std::make_shared<LinkSingle>(io);
    Bus bus{ link };
};

TEST_F(BusTest, nop)
{
    checkSendFrameSimple(Command::NOP);
    handleReplySimple();

    bus.sendNop([](DatagramState const&){});
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

    checkSendFrameSimple(Command::FPRD);
    io->handleReply<ErrorCounters>({counters});

    bus.sendRefreshErrorCounters([](DatagramState const&){});
    bus.processAwaitingFrames();

    auto const& slave = bus.slaves().at(0);
    ASSERT_EQ(34, slave.error_counters.rx[0].physical_layer);
    ASSERT_EQ(17, slave.error_counters.rx[0].invalid_frame);
    ASSERT_EQ(3,  slave.error_counters.lost_link[0]);
}


TEST_F(BusTest, logical_cmd)
{
    InSequence s;

    auto& slave = bus.slaves().at(0);
    slave.supported_mailbox = eeprom::MailboxProtocol::None; // disable mailbox protocol to use SII PDO mapping

    checkSendFrameSimple(Command::FPWR);
    io->handleReply<uint8_t>({2, 3});

    uint8_t iomap[64];
    bus.createMapping(iomap);

    // note: bit size is round up
    ASSERT_EQ(32,  slave.input.bsize);
    ASSERT_EQ(255, slave.input.size);
    ASSERT_EQ(48,  slave.output.bsize);
    ASSERT_EQ(383, slave.output.size);

    // test logical read/write/read and write

    int64_t logical_read = 0x0001020304050607;
    checkSendFrameSimple(Command::LRD);
    io->handleReply<int64_t>({logical_read});
    bus.processDataRead([](DatagramState const&){});

    for (int i = 0; i < 8; ++i)
    {
        ASSERT_EQ(7 - i, slave.input.data[i]);
    }

    int64_t logical_write = 0x0706050403020100;
    std::memcpy(slave.output.data, &logical_write, sizeof(int64_t));
    io->checkSendFrame(Command::LWR, logical_write);
    io->handleReply<int64_t>({logical_write});
    bus.processDataWrite([](DatagramState const&){});

    for (int i = 0; i < 8; ++i)
    {
        ASSERT_EQ(7 - i, slave.input.data[i]);
    }

    logical_read  = 0x1011121314151617;
    logical_write = 0x1716151413121110;
    std::memcpy(slave.output.data, &logical_write, sizeof(int64_t));
    io->checkSendFrame(Command::LRW, logical_write);
    io->handleReply<int64_t>({logical_read});
    bus.processDataReadWrite([](DatagramState const&){});

    for (int i = 0; i < 8; ++i)
    {
        ASSERT_EQ(0x17 - i, slave.input.data[i]);
    }

    // check error callbacks
    checkSendFrameSimple(Command::LRD);
    handleReplySimple(0);
    ASSERT_THROW(bus.processDataRead([](DatagramState const&){ throw std::out_of_range(""); }), std::out_of_range);

    checkSendFrameSimple(Command::LWR);
    handleReplySimple(0);
    ASSERT_THROW(bus.processDataWrite([](DatagramState const&){ throw std::logic_error(""); }), std::logic_error);

    checkSendFrameSimple(Command::LRW);
    handleReplySimple(0);
    ASSERT_THROW(bus.processDataReadWrite([](DatagramState const&){ throw std::overflow_error(""); }), std::overflow_error);
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

    checkSendFrameSimple(Command::FPRD);
    io->handleReply<Feedback>({al});

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
    checkSendFrameSimple(Command::FPRD);
    io->handleReply<Feedback>({al}, 0);
    bus.getCurrentState(slave);
    ASSERT_EQ(State::INVALID, slave.al_status);

    checkSendFrameSimple(Command::FPRD);
    al.status = 0x1;
    io->handleReply<Feedback>({al});
    ASSERT_THROW(bus.waitForState(State::OPERATIONAL, 0ns), Error);

    checkSendFrameSimple(Command::BWR);
    handleReplySimple(0);
    ASSERT_THROW(bus.requestState(State::INIT), Error);
}


TEST_F(BusTest, messages_errors)
{
    auto& slave = bus.slaves().at(0);
    slave.mailbox.can_read = true;

    checkSendFrameSimple(Command::FPRD);
    handleReplySimple(0);
    bus.sendReadMessages([](DatagramState const&){ throw std::logic_error(""); });
    ASSERT_THROW(bus.processAwaitingFrames(), std::logic_error);

    checkSendFrameSimple(Command::FPRD);
    handleReplySimple();
    bus.sendReadMessages([](DatagramState const&){ throw std::out_of_range(""); });
    ASSERT_THROW(bus.processAwaitingFrames(), std::out_of_range);
}


TEST_F(BusTest, write_SDO_OK)
{
    InSequence s;

    int32_t data = 0xCAFEDECA;
    uint32_t data_size = sizeof(data);
    auto& slave = bus.slaves().at(0);

    checkSendFrameSimple(Command::FPRD);
    io->handleReply<uint8_t>({0x08, 0});// cannot write, nothing to read

    checkSendFrameSimple(Command::FPRD);
    io->handleReply<uint8_t>({0, 0});   // can write, nothing to read

    checkSendFrameSimple(Command::FPWR);  // write to mailbox
    handleReplySimple();

    checkSendFrameSimple(Command::FPRD);
    io->handleReply<uint8_t>({0, 0});   // can write, nothing to read

    checkSendFrameSimple(Command::FPRD);
    io->handleReply<uint8_t>({0, 0x08});// can write, somethin to read

    SDOAnswer answer;
    answer.header.len = 10;
    answer.header.type = mailbox::Type::CoE;
    answer.sdo.service = CoE::Service::SDO_RESPONSE;
    answer.sdo.command = CoE::SDO::response::DOWNLOAD;
    answer.sdo.index = 0x1018;
    answer.sdo.subindex = 1;

    checkSendFrameSimple(Command::FPRD);
    io->handleReply<SDOAnswer>({answer}); // read answer

    bus.writeSDO(slave, 0x1018, 1, false, &data, data_size);
}

TEST_F(BusTest, write_SDO_timeout)
{
    InSequence s;

    int32_t data = 0xCAFEDECA;
    uint32_t data_size = sizeof(data);
    auto& slave = bus.slaves().at(0);

    checkSendFrameSimple(Command::FPRD);
    io->handleReply<uint8_t>({0x08, 0});// cannot write, nothing to read

    ASSERT_THROW(bus.writeSDO(slave, 0x1018, 1, false, &data, data_size, 0ns), Error);
}

TEST_F(BusTest, write_SDO_bad_answer)
{
    InSequence s;

    int32_t data = 0xCAFEDECA;
    uint32_t data_size = sizeof(data);
    auto& slave = bus.slaves().at(0);

    checkSendFrameSimple(Command::FPRD);
    io->handleReply<uint8_t>({0, 0});// can write, nothing to read

    checkSendFrameSimple(Command::FPWR);  // write to mailbox
    handleReplySimple(0);

    ASSERT_THROW(bus.writeSDO(slave, 0x1018, 1, false, &data, data_size), Error);
}

TEST_F(BusTest, read_SDO_OK)
{
    InSequence s;

    int32_t data = 0;
    uint32_t data_size = sizeof(data);
    auto& slave = bus.slaves().at(0);

    checkSendFrameSimple(Command::FPRD);
    io->handleReply<uint8_t>({0, 0});// can write, nothing to read

    checkSendFrameSimple(Command::FPWR);  // write to mailbox
    handleReplySimple();

    checkSendFrameSimple(Command::FPRD);
    io->handleReply<uint8_t>({0, 0x08});// can write, somethin to read

    SDOAnswer answer;
    answer.header.len = 10;
    answer.header.type = mailbox::Type::CoE;
    answer.sdo.service = CoE::Service::SDO_RESPONSE;
    answer.sdo.command = CoE::SDO::response::UPLOAD;
    answer.sdo.index = 0x1018;
    answer.sdo.subindex = 1;
    answer.sdo.transfer_type = 1;
    answer.sdo.block_size = 0;
    *reinterpret_cast<uint32_t*>(answer.payload) = 0xDEADBEEF;

    checkSendFrameSimple(Command::FPRD);
    io->handleReply<SDOAnswer>({answer}); // read answer

    bus.readSDO(slave, 0x1018, 1, Bus::Access::PARTIAL, &data, &data_size);
    ASSERT_EQ(0xDEADBEEF, data);
    ASSERT_EQ(4, data_size);
}

TEST_F(BusTest, read_SDO_emulated_complete_access_OK)
{
    addReadEmulatedSDO<uint32_t>(0x1018, { 3, 0xCAFE0000, 0x0000DECA, 0xFADEFACE });

    auto& slave = bus.slaves().at(0);
    uint32_t data[3] = {0};
    uint32_t data_size = sizeof(data);

    bus.readSDO(slave, 0x1018, 1, Bus::Access::EMULATE_COMPLETE, &data, &data_size);
    ASSERT_EQ(0xCAFE0000, data[0]);
    ASSERT_EQ(0x0000DECA, data[1]);
    ASSERT_EQ(0xFADEFACE, data[2]);
    ASSERT_EQ(12, data_size);
}


TEST_F(BusTest, read_SDO_buffer_too_small)
{
    addReadEmulatedSDO<uint32_t>(0x1018, { 3, 0xCAFE0000 });

    auto& slave = bus.slaves().at(0);
    uint32_t data = {0};
    uint32_t data_size = sizeof(data);

    ASSERT_THROW(bus.readSDO(slave, 0x1018, 1, Bus::Access::EMULATE_COMPLETE, &data, &data_size), Error);
}


TEST_F(BusTest, detect_mapping_CoE)
{
    InSequence s;

    addReadEmulatedSDO<uint8_t>(CoE::SM_COM_TYPE,    { 2, SyncManagerType::Output, SyncManagerType::Input});

    addReadEmulatedSDO<uint16_t>(CoE::SM_CHANNEL + 0, { 2, 0x1A0A, 0x1A0B });
    addReadEmulatedSDO<uint32_t>(0x1A0A, { 2,  8, 8 });
    addReadEmulatedSDO<uint32_t>(0x1A0B, { 2, 16, 8 });

    addReadEmulatedSDO<uint16_t>(CoE::SM_CHANNEL + 1, { 2, 0x160A, 0x160B });
    addReadEmulatedSDO<uint32_t>(0x160A, { 2, 16, 16 });
    addReadEmulatedSDO<uint32_t>(0x160B, { 2, 32, 16 });

    // SM/FMMU configuration
    checkSendFrameSimple(Command::FPWR);
    io->handleReply<uint8_t>({2, 3});

    uint8_t iomap[64];
    bus.createMapping(iomap);

    auto& slave = bus.slaves().at(0);
    ASSERT_EQ(5,  slave.output.bsize);
    ASSERT_EQ(10, slave.input.bsize);
}


TEST_F(BusTest, pdio_watchdogs)
{
    auto clearForInit = [&]()
    {
        ASSERT_EQ(1, bus.detectedSlaves());
        auto& slave = bus.slaves().at(0);
        slave.sii.TxPDO.clear();
        slave.sii.RxPDO.clear();
    };
    clearForInit();
    initBus(0ms);
    clearForInit();
    initBus(1234ms);
    clearForInit();

    detectAndReset();
    ASSERT_THROW(bus.init(10s), Error);
    detectAndReset();
    ASSERT_THROW(bus.init(-1s), Error);
}


TEST_F(BusTest, init_no_slave_detected)
{
    checkSendFrameSimple(Command::BRD);
    handleReplySimple(0);
    ASSERT_THROW(bus.init(), Error);
}

TEST_F(BusTest, send_get_DL_status)
{
    auto& slave = bus.slaves().at(0);
    checkSendFrameSimple(Command::FPRD);
    io->handleReply<uint16_t>({0x0530});

    bus.sendGetDLStatus(slave);
    ASSERT_EQ(slave.dl_status.PL_port0, 1);
    ASSERT_EQ(slave.dl_status.PL_port1, 1);
    ASSERT_EQ(slave.dl_status.PL_port2, 0);
    ASSERT_EQ(slave.dl_status.PL_port3, 0);
    ASSERT_EQ(slave.dl_status.COM_port0, 0);
    ASSERT_EQ(slave.dl_status.COM_port1, 0);
    ASSERT_EQ(slave.dl_status.LOOP_port0, 1);
    ASSERT_EQ(slave.dl_status.LOOP_port1, 1);
}
