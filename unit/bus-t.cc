#include <gtest/gtest.h>
#include <cstring>

#include "kickcat/Link.h"
#include "kickcat/SocketNull.h"
#include "kickcat/Bus.h"
#include "Mocks.h"

using ::testing::Return;
using ::testing::_;
using ::testing::Invoke;
using ::testing::InSequence;

using namespace kickcat;

struct SDOAnswer
{
    mailbox::Header header;
    CoE::Header coe;
    CoE::ServiceData sdo;
    uint8_t payload[4];
} __attribute__((__packed__));



// All the bus test are done like the redundancy is not activated (working only on the nominal interface).
class BusTest : public testing::Test
{
public:
    void SetUp() override
    {
        EXPECT_CALL(*io_nominal, setTimeout(::testing::_))
            .WillRepeatedly(Return());
        bus.configureWaitLatency(0ns, 0ns);
        initBus();
    }

    template<typename T>
    void checkSendFrame(Command cmd, T payload, int32_t n = 1)
    {
        std::vector<DatagramCheck<T>> expecteds(n, {cmd, payload, false});
        io_nominal->checkSendFrame(expecteds);
    }

    void checkSendFrameSimple(Command cmd, int32_t n = 1)
    {
        checkSendFrame<uint8_t>(cmd, 0, n);
    }

    void handleReplyWriteThenRead(uint16_t wkc = 1)
    {
        io_nominal->handleReply<uint8_t>({0}, wkc);
        handleReplyFailReadRedFrame();
    }

    void handleReplyFailReadRedFrame()
    {
        EXPECT_CALL(*io_nominal, read(_, _))
        .WillOnce(Invoke([this](uint8_t*, int32_t)
        {
            return 0;
        }));
    }


    void handleReplySimple(uint16_t wkc = 1)
    {
        io_nominal->handleReply<uint8_t>({0}, wkc);
    }

    void addFetchEepromWord(uint32_t word)
    {
        // address
        checkSendFrameSimple(Command::BWR);
        handleReplyWriteThenRead();

        // eeprom ready
        checkSendFrameSimple(Command::FPRD);
        io_nominal->handleReply<uint16_t>({0x0080}); // 0x8000 in LE

        // fetch reply
        checkSendFrameSimple(Command::FPRD);
        io_nominal->handleReply<uint32_t>({word});
    }

    void detectAndReset()
    {
        InSequence s;
        // detect slaves
        checkSendFrameSimple(Command::BRD);
        handleReplyWriteThenRead();

        // reset slaves
        for (int i = 0; i < 9; ++i)
        {
            checkSendFrameSimple(Command::BWR);
            handleReplyWriteThenRead();
        }
        for (int i = 0; i < 3; ++i)
        {
            checkSendFrameSimple(Command::BRD);
            handleReplyWriteThenRead();
        }
    }

    void initBus(milliseconds watchdog = 100ms)
    {
        InSequence s;

        detectAndReset();

        // PDIO Watchdog
        uint16_t watchdogTimeCheck = static_cast<uint16_t>(watchdog / 100us);

        std::vector<DatagramCheck<uint16_t>> expecteds_1(1, {Command::BWR, uint16_t(0x09C2)});
        io_nominal->checkSendFrame(expecteds_1);
        handleReplyWriteThenRead();

        std::vector<DatagramCheck<uint16_t>> expecteds_2(1, {Command::BWR, watchdogTimeCheck});
        io_nominal->checkSendFrame(expecteds_2);
        handleReplyWriteThenRead();
        io_nominal->checkSendFrame(expecteds_2);
        handleReplyWriteThenRead();

        // eeprom to master
        checkSendFrameSimple(Command::BWR);
        handleReplyWriteThenRead();

        // set addresses
        checkSendFrameSimple(Command::APWR);
        handleReplyWriteThenRead();

        // request state: INIT
        checkSendFrameSimple(Command::BWR);
        handleReplyWriteThenRead();

        // check state
        checkSendFrameSimple(Command::FPRD);
        io_nominal->handleReply<uint8_t>({State::INIT});

        // fetch eeprom
        addFetchEepromWord(0);
        addFetchEepromWord(0);
        addFetchEepromWord(0);
        addFetchEepromWord(0);

        addFetchEepromWord(0xCAFEDECA);     // vendor id
        addFetchEepromWord(0xA5A5A5A5);     // product code
        addFetchEepromWord(0x5A5A5A5A);     // revision number
        addFetchEepromWord(0x12345678);     // serial number
        addFetchEepromWord(0);              // hardware delay
        addFetchEepromWord(0);              // hardware delay
        addFetchEepromWord(0);              // bootstrap mailbox
        addFetchEepromWord(0);              // bootstrap mailbox
        addFetchEepromWord(0x01001000);     // mailbox rcv offset + size
        addFetchEepromWord(0x02002000);     // mailbox snd offset + size
        addFetchEepromWord(4);              // mailbox protocol: CoE
        addFetchEepromWord(0);              // eeprom size

        for (int i=0; i<18; ++i)
        {
            addFetchEepromWord(0);
        }

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
        addFetchEepromWord(0x00001000);     //
        addFetchEepromWord(0x03010064);     //
        addFetchEepromWord(0x00001200);     //
        addFetchEepromWord(0x04010020);     //

        addFetchEepromWord(0xFFFFFFFF);     // end of eeprom

        // configue mailbox:
        checkSendFrameSimple(Command::FPWR);
        handleReplySimple();

        // request state: PREOP
        checkSendFrameSimple(Command::BWR);
        handleReplyWriteThenRead();

        // check state
        checkSendFrameSimple(Command::FPRD);
        io_nominal->handleReply<uint8_t>({State::PRE_OP});

        // clear mailbox
        checkSendFrameSimple(Command::FPRD, 2);
        io_nominal->handleReply<uint8_t>({0x08, 0}); // can write, nothing to read

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
        answer.header.address = 0;
        answer.header.type = mailbox::Type::CoE;
        answer.coe.service = CoE::Service::SDO_RESPONSE;
        answer.sdo.command = CoE::SDO::response::UPLOAD;
        answer.sdo.index = index;
        answer.sdo.transfer_type = 1;
        answer.sdo.block_size = 4 - sizeof(T);
        std::memset(answer.payload, 0, 4);

        for (uint32_t i = 0; i < data_to_reply.size(); ++i)
        {
            checkSendFrameSimple(Command::FPRD, 2);
            io_nominal->handleReply<uint8_t>({0, 0});// can write, nothing to read

            checkSendFrameSimple(Command::FPWR);  // write to mailbox
            handleReplySimple();

            checkSendFrameSimple(Command::FPRD, 2);
            io_nominal->handleReply<uint8_t>({0, 0x08});// can write, something to read

            answer.sdo.subindex = static_cast<uint8_t>(i);
            std::memcpy(answer.payload, &data_to_reply[i], sizeof(T));
            checkSendFrameSimple(Command::FPRD);
            io_nominal->handleReply<SDOAnswer>({answer}); // read answer
        }
    }

protected:
    std::shared_ptr<MockSocket> io_nominal{ std::make_shared<MockSocket>() };
    std::shared_ptr<SocketNull> io_redundancy{ std::make_shared<SocketNull>() };
    std::shared_ptr<Link> link = std::make_shared<Link>(io_nominal, io_redundancy, nullptr);
    Bus bus{ link };
};

TEST_F(BusTest, nop)
{
    checkSendFrameSimple(Command::NOP);
    handleReplySimple();

    bus.sendNop([](DatagramState const&){});
    bus.finalizeDatagrams();
    bus.processAwaitingFrames();
}


TEST_F(BusTest, error_counters)
{
    // Refresh errors counters
    ErrorCounters counters{};
    std::memset(&counters, 0, sizeof(ErrorCounters));
    counters.rx[0].invalid_frame = 17;
    counters.rx[0].physical_layer = 34;
    counters.lost_link[0] = 3;

    checkSendFrameSimple(Command::FPRD);
    io_nominal->handleReply<ErrorCounters>({counters});

    bus.sendRefreshErrorCounters([](DatagramState const&){});
    bus.processAwaitingFrames();

    auto const& slave = bus.slaves().at(0);
    ASSERT_EQ(34, slave.error_counters.rx[0].physical_layer);
    ASSERT_EQ(17, slave.error_counters.rx[0].invalid_frame);
    ASSERT_EQ(3,  slave.error_counters.lost_link[0]);

    // Error handling
    checkSendFrameSimple(Command::FPRD);
    handleReplySimple(0);

    DatagramState state = DatagramState::OK;
    auto error_callback = [&](DatagramState const& s) { state = s; };
    bus.sendRefreshErrorCounters(error_callback);
    bus.processAwaitingFrames();
    ASSERT_EQ(state, DatagramState::INVALID_WKC);
}


TEST_F(BusTest, logical_cmd)
{
    InSequence s;

    auto& slave = bus.slaves().at(0);
    slave.supported_mailbox = eeprom::MailboxProtocol::None; // disable mailbox protocol to use SII PDO mapping

    checkSendFrameSimple(Command::FPWR, 4);
    io_nominal->handleReply<uint8_t>({2, 3});

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
    io_nominal->handleReply<int64_t>({logical_read});
    bus.processDataRead([](DatagramState const&){});

    for (int i = 0; i < 8; ++i)
    {
        ASSERT_EQ(7 - i, slave.input.data[i]);
    }

    int64_t logical_write = 0x0706050403020100;
    std::memcpy(slave.output.data, &logical_write, sizeof(int64_t));
    std::vector<DatagramCheck<int64_t>> expecteds(1, {Command::LWR, logical_write});
    io_nominal->checkSendFrame(expecteds);

    io_nominal->handleReply<int64_t>({logical_write});
    bus.processDataWrite([](DatagramState const&){});

    for (int i = 0; i < 8; ++i)
    {
        ASSERT_EQ(7 - i, slave.input.data[i]);
    }

    logical_read  = 0x1011121314151617;
    logical_write = 0x1716151413121110;
    std::memcpy(slave.output.data, &logical_write, sizeof(int64_t));
    std::vector<DatagramCheck<int64_t>> expecteds_2(1, {Command::LRW, logical_write});
    io_nominal->checkSendFrame(expecteds_2);
    io_nominal->handleReply<int64_t>({logical_read}, 3);
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
    io_nominal->handleReply<Feedback>({al});

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
    io_nominal->handleReply<Feedback>({al}, 0);
    bus.getCurrentState(slave);
    ASSERT_EQ(State::INVALID, slave.al_status);

    checkSendFrameSimple(Command::FPRD);
    al.status = 0x1;
    io_nominal->handleReply<Feedback>({al});
    ASSERT_THROW(bus.waitForState(State::OPERATIONAL, 0ns), Error);

    checkSendFrameSimple(Command::BWR);
    handleReplyWriteThenRead(0);
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

    checkSendFrameSimple(Command::FPRD, 2);
    io_nominal->handleReply<uint8_t>({0x08, 0});// cannot write, nothing to read

    checkSendFrameSimple(Command::FPRD, 2);
    io_nominal->handleReply<uint8_t>({0, 0});   // can write, nothing to read

    checkSendFrameSimple(Command::FPWR);  // write to mailbox
    handleReplySimple();

    checkSendFrameSimple(Command::FPRD, 2);
    io_nominal->handleReply<uint8_t>({0, 0});   // can write, nothing to read

    checkSendFrameSimple(Command::FPRD, 2);
    io_nominal->handleReply<uint8_t>({0, 0x08});// can write, somethin to read

    SDOAnswer answer;
    answer.header.len = 10;
    answer.header.address = 0;
    answer.header.type = mailbox::Type::CoE;
    answer.coe.service = CoE::Service::SDO_RESPONSE;
    answer.sdo.command = CoE::SDO::response::DOWNLOAD;
    answer.sdo.index = 0x1018;
    answer.sdo.subindex = 1;

    checkSendFrameSimple(Command::FPRD);
    io_nominal->handleReply<SDOAnswer>({answer}); // read answer

    bus.writeSDO(slave, 0x1018, 1, false, &data, data_size);
}

TEST_F(BusTest, write_SDO_timeout)
{
    InSequence s;

    int32_t data = 0xCAFEDECA;
    uint32_t data_size = sizeof(data);
    auto& slave = bus.slaves().at(0);

    checkSendFrameSimple(Command::FPRD, 2);
    io_nominal->handleReply<uint8_t>({0x08, 0});// cannot write, nothing to read

    ASSERT_THROW(bus.writeSDO(slave, 0x1018, 1, false, &data, data_size, 1ms), Error);
}

TEST_F(BusTest, write_SDO_bad_answer)
{
    InSequence s;

    int32_t data = 0xCAFEDECA;
    uint32_t data_size = sizeof(data);
    auto& slave = bus.slaves().at(0);

    checkSendFrameSimple(Command::FPRD, 2);
    io_nominal->handleReply<uint8_t>({0, 0});// can write, nothing to read

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

    checkSendFrameSimple(Command::FPRD, 2);
    io_nominal->handleReply<uint8_t>({0, 0});// can write, nothing to read

    checkSendFrameSimple(Command::FPWR);  // write to mailbox
    handleReplySimple();

    checkSendFrameSimple(Command::FPRD, 2);
    io_nominal->handleReply<uint8_t>({0, 0x08});// can write, somethin to read

    SDOAnswer answer;
    answer.header.len = 10;
    answer.header.address = 0;
    answer.header.type = mailbox::Type::CoE;
    answer.coe.service = CoE::Service::SDO_RESPONSE;
    answer.sdo.command = CoE::SDO::response::UPLOAD;
    answer.sdo.index = 0x1018;
    answer.sdo.subindex = 1;
    answer.sdo.transfer_type = 1;
    answer.sdo.block_size = 0;
    *reinterpret_cast<uint32_t*>(answer.payload) = 0xDEADBEEF;

    checkSendFrameSimple(Command::FPRD);
    io_nominal->handleReply<SDOAnswer>({answer}); // read answer

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
    checkSendFrameSimple(Command::FPWR, 4);
    io_nominal->handleReply<uint8_t>({2, 3});

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
    InSequence s;
    checkSendFrameSimple(Command::BRD);
    handleReplyWriteThenRead(0);
    ASSERT_THROW(bus.init(), Error);
}

TEST_F(BusTest, send_get_DL_status)
{
    auto& slave = bus.slaves().at(0);
    checkSendFrameSimple(Command::FPRD);
    io_nominal->handleReply<uint16_t>({0x0530});

    bus.sendGetDLStatus(slave, [](DatagramState const&){});
    bus.processAwaitingFrames();
    ASSERT_EQ(slave.dl_status.PL_port0, 1);
    ASSERT_EQ(slave.dl_status.PL_port1, 1);
    ASSERT_EQ(slave.dl_status.PL_port2, 0);
    ASSERT_EQ(slave.dl_status.PL_port3, 0);
    ASSERT_EQ(slave.dl_status.COM_port0, 0);
    ASSERT_EQ(slave.dl_status.COM_port1, 0);
    ASSERT_EQ(slave.dl_status.LOOP_port0, 1);
    ASSERT_EQ(slave.dl_status.LOOP_port1, 1);

    // Error handling
    checkSendFrameSimple(Command::FPRD);
    handleReplySimple(0);

    DatagramState state = DatagramState::OK;
    auto error_callback = [&](DatagramState const& s) { state = s; };
    bus.sendGetDLStatus(slave, error_callback);
    bus.processAwaitingFrames();
    ASSERT_EQ(state, DatagramState::INVALID_WKC);
}


TEST_F(BusTest, IRQ_OK)
{
    InSequence s;

    checkSendFrame(Command::BWR, uint16_t(EcatEvent::DL_STATUS));
    handleReplyWriteThenRead();

    checkSendFrame(Command::BWR, uint16_t(EcatEvent::DL_STATUS | EcatEvent::AL_STATUS));
    handleReplyWriteThenRead();

    checkSendFrame(Command::BWR, uint16_t(EcatEvent::AL_STATUS));
    handleReplyWriteThenRead();

    checkSendFrame(Command::BWR, uint16_t(0));
    handleReplyWriteThenRead();

    bus.enableIRQ(EcatEvent::DL_STATUS, [](){});
    bus.enableIRQ(EcatEvent::AL_STATUS, [](){});

    bus.disableIRQ(EcatEvent::DL_STATUS);
    bus.disableIRQ(EcatEvent::AL_STATUS);
}


TEST_F(BusTest, IRQ_NOK)
{
    InSequence s;

    checkSendFrame(Command::BWR, uint16_t(EcatEvent::DL_STATUS));
    handleReplyWriteThenRead(0);

    checkSendFrame(Command::BWR, uint16_t(0));
    handleReplyWriteThenRead(0);

    ASSERT_THROW(bus.enableIRQ(EcatEvent::DL_STATUS, [](){}), kickcat::Error);
    ASSERT_THROW(bus.disableIRQ(EcatEvent::DL_STATUS),        kickcat::Error);
}


TEST_F(BusTest, add_gateway_message)
{
    constexpr uint16_t GATEWAY_INDEX = 42;
    Mailbox mailbox;
    mailbox.recv_size = 128;

    // Create a standard SDO with a non local address
    int32_t data = 0xCAFEDECA;
    uint32_t data_size = sizeof(data);
    auto msg = mailbox.createSDO(0x1018, 1, false, CoE::SDO::request::UPLOAD, &data, &data_size);

    // Master mailbox targeted
    msg->setAddress(0);
    ASSERT_EQ(nullptr, bus.addGatewayMessage(msg->data(), msg->size(), GATEWAY_INDEX));

    // Unknown slave targeted
    msg->setAddress(1002);
    ASSERT_EQ(nullptr, bus.addGatewayMessage(msg->data(), msg->size(), GATEWAY_INDEX));

    // First slave targeted
    msg->setAddress(1001);
    auto gw_msg = bus.addGatewayMessage(msg->data(), msg->size(), GATEWAY_INDEX);
    ASSERT_NE(nullptr, gw_msg);
    ASSERT_EQ(GATEWAY_INDEX, gw_msg->gatewayIndex());
}


TEST_F(BusTest, clearErrorCounters_wkc_error)
{
    InSequence s;

    checkSendFrame(Command::BWR, uint16_t(0));
    handleReplyWriteThenRead(0);
    ASSERT_THROW(bus.clearErrorCounters();, kickcat::Error);
}
