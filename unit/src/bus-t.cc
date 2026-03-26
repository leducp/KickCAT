#include <gtest/gtest.h>
#include "mocks/Link.h"
#include "mocks/Time.h"

#include "kickcat/Bus.h"
#include "kickcat/MailboxSequencer.h"

using namespace kickcat;

struct SDOAnswer
{
    mailbox::Header header;
    CoE::Header coe;
    CoE::ServiceData sdo;
    uint8_t payload[4];
} __attribute__((__packed__));


class BusTest : public testing::Test
{
public:
    void SetUp() override
    {
        resetSinceEpoch();
        bus.configureWaitLatency(0ns, 0ns);
        initBus();
    }

    void addFetchEepromWord(uint32_t word)
    {
        // broadcastWrite: set address
        mock_link->handleWriteThenRead(Command::BWR, 1);

        // areEepromReady: check status (not busy)
        mock_link->handleProcess(Command::FPRD, uint16_t{0x0080}, 1);

        // readEeprom: fetch data word
        mock_link->handleProcess(Command::FPRD, word, 1);
    }

    void detectAndReset()
    {
        // detectSlaves: broadcastRead
        mock_link->handleWriteThenRead(Command::BRD, 1);

        // reset slaves
        for (int i = 0; i < 11; ++i)
        {
            mock_link->handleWriteThenRead(Command::BWR, 1);
        }
        for (int i = 0; i < 3; ++i)
        {
            mock_link->handleWriteThenRead(Command::BRD, 1);
        }
    }

    void initBus(milliseconds watchdog = 100ms)
    {
        detectAndReset();

        // PDIO Watchdog: 3 broadcastWrites (divider, time PDI, time PDO)
        mock_link->handleWriteThenRead(Command::BWR, 1);
        mock_link->handleWriteThenRead(Command::BWR, 1);
        mock_link->handleWriteThenRead(Command::BWR, 1);

        // eeprom to master
        mock_link->handleWriteThenRead(Command::BWR, 1);

        // setAddresses
        mock_link->handleWriteThenRead(Command::APWR, 1);

        // fetchESC
        mock_link->handleProcess(Command::FPRD, ESC::Description{0x4, 0, 0, 8, 8, 16, 0xf, 0x1cc}, 1);

        // fetch DL status
        mock_link->handleProcess(Command::FPRD, uint16_t(0x0030), 1); // PL_port0 and PL_port1 active

        // requestState: INIT
        mock_link->handleWriteThenRead(Command::BWR, 1);

        // waitForState: check state INIT
        mock_link->handleProcess(Command::FPRD, uint8_t(State::INIT), 1);

        // fetchEeprom
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

        for (int i = 0; i < 18; ++i)
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
        addFetchEepromWord(0x00001000);
        addFetchEepromWord(0x03010064);
        addFetchEepromWord(0x00001200);
        addFetchEepromWord(0x04010020);

        addFetchEepromWord(0xFFFFFFFF);     // end of eeprom

        // configureMailboxes
        mock_link->handleProcess(Command::FPWR, uint8_t{0}, 1);

        // requestState: PREOP
        mock_link->handleWriteThenRead(Command::BWR, 1);

        // waitForState: check state PREOP
        mock_link->handleProcess(Command::FPRD, uint8_t(State::PRE_OP), 1);

        // checkMailboxes: write check + read check
        mock_link->handleProcess(Command::FPRD, uint8_t{0x08}, 1);
        mock_link->handleProcess(Command::FPRD, uint8_t{0}, 1);

        bus.init(watchdog);

        ASSERT_EQ(1, bus.detectedSlaves());

        auto const& slave = bus.slaves().at(0);
        ASSERT_EQ(0xCAFEDECA, slave.sii.vendor_id);
        ASSERT_EQ(0xA5A5A5A5, slave.sii.product_code);
        ASSERT_EQ(0x5A5A5A5A, slave.sii.revision_number);
        ASSERT_EQ(0x12345678, slave.sii.serial_number);
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
            // checkMailboxes: can write, nothing to read
            mock_link->handleProcess(Command::FPRD, uint8_t{0}, 1);
            mock_link->handleProcess(Command::FPRD, uint8_t{0}, 1);

            // write to mailbox
            mock_link->handleProcess(Command::FPWR, uint8_t{0}, 1);

            // checkMailboxes: can write, something to read
            mock_link->handleProcess(Command::FPRD, uint8_t{0}, 1);
            mock_link->handleProcess(Command::FPRD, uint8_t{0x08}, 1);

            answer.sdo.subindex = static_cast<uint8_t>(i);
            std::memcpy(answer.payload, &data_to_reply[i], sizeof(T));

            // read answer
            mock_link->handleProcess(Command::FPRD, answer, 1);
        }
    }

protected:
    std::shared_ptr<MockLink> mock_link{ std::make_shared<MockLink>() };
    Bus bus{ mock_link };
};

TEST_F(BusTest, nop)
{
    mock_link->handleProcess(Command::NOP, uint8_t{0}, 1);

    bus.sendNop([](DatagramState const&){});
    bus.finalizeDatagrams();
    bus.processAwaitingFrames();
}


TEST_F(BusTest, error_counters)
{
    ErrorCounters counters{};
    std::memset(&counters, 0, sizeof(ErrorCounters));
    counters.rx[0].invalid_frame = 17;
    counters.rx[0].physical_layer = 34;
    counters.lost_link[0] = 3;

    mock_link->handleProcess(Command::FPRD, counters, 1);

    bus.sendRefreshErrorCounters([](DatagramState const&){});
    bus.processAwaitingFrames();

    auto const& slave = bus.slaves().at(0);
    ASSERT_EQ(34, slave.error_counters.rx[0].physical_layer);
    ASSERT_EQ(17, slave.error_counters.rx[0].invalid_frame);
    ASSERT_EQ(3,  slave.error_counters.lost_link[0]);

    // Error handling
    mock_link->handleProcess(Command::FPRD, uint8_t{0}, 0);

    DatagramState state = DatagramState::OK;
    auto error_callback = [&](DatagramState const& s) { state = s; };
    bus.sendRefreshErrorCounters(error_callback);
    bus.processAwaitingFrames();
    ASSERT_EQ(state, DatagramState::INVALID_WKC);
}


TEST_F(BusTest, logical_cmd)
{
    auto& slave = bus.slaves().at(0);
    slave.sii.supported_mailbox = eeprom::MailboxProtocol::None;

    // configureFMMUs: 4 datagrams (SM+FMMU for input + SM+FMMU for output), all wkc=1
    for (int i = 0; i < 4; ++i)
    {
        mock_link->handleProcess(Command::FPWR, uint8_t{0}, 1);
    }

    uint8_t iomap[64];
    bus.createMapping(iomap);

    ASSERT_EQ(32,  slave.input.bsize);
    ASSERT_EQ(255, slave.input.size);
    ASSERT_EQ(48,  slave.output.bsize);
    ASSERT_EQ(383, slave.output.size);

    // test logical read
    int64_t logical_read = 0x0001020304050607;
    mock_link->handleProcess(Command::LRD, logical_read, 1);
    bus.processDataRead([](DatagramState const&){});

    for (int i = 0; i < 8; ++i)
    {
        ASSERT_EQ(7 - i, slave.input.data[i]);
    }

    // test logical write
    int64_t logical_write = 0x0706050403020100;
    std::memcpy(slave.output.data, &logical_write, sizeof(int64_t));
    mock_link->handleProcess(Command::LWR, logical_write, 1);
    bus.processDataWrite([](DatagramState const&){});

    for (int i = 0; i < 8; ++i)
    {
        ASSERT_EQ(7 - i, slave.input.data[i]);
    }

    // test logical read-write
    logical_read  = 0x1011121314151617;
    logical_write = 0x1716151413121110;
    std::memcpy(slave.output.data, &logical_write, sizeof(int64_t));
    mock_link->handleProcess(Command::LRW, logical_read, 3);
    bus.processDataReadWrite([](DatagramState const&){});

    for (int i = 0; i < 8; ++i)
    {
        ASSERT_EQ(0x17 - i, slave.input.data[i]);
    }

    // check error callbacks
    mock_link->handleProcess(Command::LRD, uint8_t{0}, 0);
    ASSERT_THROW(bus.processDataRead([](DatagramState const&){ throw std::out_of_range(""); }), std::out_of_range);

    mock_link->handleProcess(Command::LWR, uint8_t{0}, 0);
    ASSERT_THROW(bus.processDataWrite([](DatagramState const&){ throw std::logic_error(""); }), std::logic_error);

    mock_link->handleProcess(Command::LRW, uint8_t{0}, 0);
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

    mock_link->handleProcess(Command::FPRD, al, 1);

    try
    {
        bus.getCurrentState(slave);
    }
    catch(ErrorAL const & error)
    {
        ASSERT_EQ(0x0020, error.code());
    }
    ASSERT_EQ(0x11, slave.al_status);

    slave.al_status = State::INVALID;
    mock_link->handleProcess(Command::FPRD, al, 0);
    bus.getCurrentState(slave);
    ASSERT_EQ(State::INVALID, slave.al_status);

    al.status = 0x1;
    mock_link->handleProcess(Command::FPRD, al, 1);
    ASSERT_THROW(bus.waitForState(State::OPERATIONAL, 0ns), Error);

    mock_link->handleWriteThenRead(Command::BWR, 0);
    ASSERT_THROW(bus.requestState(State::INIT), Error);
}


TEST_F(BusTest, messages_errors)
{
    auto& slave = bus.slaves().at(0);
    slave.mailbox.can_read = true;

    mock_link->handleProcess(Command::FPRD, uint8_t{0}, 0);
    bus.sendReadMessages([](DatagramState const&){ throw std::logic_error(""); });
    ASSERT_THROW(bus.processAwaitingFrames(), std::logic_error);

    mock_link->handleProcess(Command::FPRD, uint8_t{0}, 1);
    bus.sendReadMessages([](DatagramState const&){ throw std::out_of_range(""); });
    ASSERT_THROW(bus.processAwaitingFrames(), std::out_of_range);
}


TEST_F(BusTest, write_SDO_OK)
{
    int32_t data = 0xCAFEDECA;
    uint32_t data_size = sizeof(data);
    auto& slave = bus.slaves().at(0);

    // checkMailboxes: cannot write, nothing to read
    mock_link->handleProcess(Command::FPRD, uint8_t{0x08}, 1);
    mock_link->handleProcess(Command::FPRD, uint8_t{0}, 1);

    // checkMailboxes: can write, nothing to read
    mock_link->handleProcess(Command::FPRD, uint8_t{0}, 1);
    mock_link->handleProcess(Command::FPRD, uint8_t{0}, 1);

    // write to mailbox
    mock_link->handleProcess(Command::FPWR, uint8_t{0}, 1);

    // checkMailboxes: can write, nothing to read
    mock_link->handleProcess(Command::FPRD, uint8_t{0}, 1);
    mock_link->handleProcess(Command::FPRD, uint8_t{0}, 1);

    // checkMailboxes: can write, something to read
    mock_link->handleProcess(Command::FPRD, uint8_t{0}, 1);
    mock_link->handleProcess(Command::FPRD, uint8_t{0x08}, 1);

    SDOAnswer answer;
    answer.header.len = 10;
    answer.header.address = 0;
    answer.header.type = mailbox::Type::CoE;
    answer.coe.service = CoE::Service::SDO_RESPONSE;
    answer.sdo.command = CoE::SDO::response::DOWNLOAD;
    answer.sdo.index = 0x1018;
    answer.sdo.subindex = 1;

    // read answer
    mock_link->handleProcess(Command::FPRD, answer, 1);

    bus.writeSDO(slave, 0x1018, 1, Bus::Access::PARTIAL, &data, data_size);
}

TEST_F(BusTest, write_SDO_timeout)
{
    int32_t data = 0xCAFEDECA;
    uint32_t data_size = sizeof(data);
    auto& slave = bus.slaves().at(0);

    // checkMailboxes: cannot write, nothing to read
    mock_link->handleProcess(Command::FPRD, uint8_t{0x08}, 1);
    mock_link->handleProcess(Command::FPRD, uint8_t{0}, 1);

    ASSERT_THROW(bus.writeSDO(slave, 0x1018, 1, Bus::Access::PARTIAL, &data, data_size, 1ms), Error);
}

TEST_F(BusTest, write_SDO_bad_answer)
{
    int32_t data = 0xCAFEDECA;
    uint32_t data_size = sizeof(data);
    auto& slave = bus.slaves().at(0);

    // checkMailboxes: can write, nothing to read
    mock_link->handleProcess(Command::FPRD, uint8_t{0}, 1);
    mock_link->handleProcess(Command::FPRD, uint8_t{0}, 1);

    // write to mailbox - fails (wkc=0)
    mock_link->handleProcess(Command::FPWR, uint8_t{0}, 0);

    ASSERT_THROW(bus.writeSDO(slave, 0x1018, 1, Bus::Access::PARTIAL, &data, data_size), Error);
}

TEST_F(BusTest, read_SDO_OK)
{
    int32_t data = 0;
    uint32_t data_size = sizeof(data);
    auto& slave = bus.slaves().at(0);

    // checkMailboxes: can write, nothing to read
    mock_link->handleProcess(Command::FPRD, uint8_t{0}, 1);
    mock_link->handleProcess(Command::FPRD, uint8_t{0}, 1);

    // write to mailbox
    mock_link->handleProcess(Command::FPWR, uint8_t{0}, 1);

    // checkMailboxes: can write, something to read
    mock_link->handleProcess(Command::FPRD, uint8_t{0}, 1);
    mock_link->handleProcess(Command::FPRD, uint8_t{0x08}, 1);

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

    // read answer
    mock_link->handleProcess(Command::FPRD, answer, 1);

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
    addReadEmulatedSDO<uint8_t>(CoE::SM_COM_TYPE,    { 2, SyncManagerType::Output, SyncManagerType::Input});

    addReadEmulatedSDO<uint16_t>(CoE::SM_CHANNEL + 0, { 2, 0x1A0A, 0x1A0B });
    addReadEmulatedSDO<uint32_t>(0x1A0A, { 2,  8, 8 });
    addReadEmulatedSDO<uint32_t>(0x1A0B, { 2, 16, 8 });

    addReadEmulatedSDO<uint16_t>(CoE::SM_CHANNEL + 1, { 2, 0x160A, 0x160B });
    addReadEmulatedSDO<uint32_t>(0x160A, { 2, 16, 16 });
    addReadEmulatedSDO<uint32_t>(0x160B, { 2, 32, 16 });

    // configureFMMUs: 4 datagrams, all wkc=1
    for (int i = 0; i < 4; ++i)
    {
        mock_link->handleProcess(Command::FPWR, uint8_t{0}, 1);
    }

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
    mock_link->handleWriteThenRead(Command::BRD, 0);
    ASSERT_THROW(bus.init(), Error);
}

TEST_F(BusTest, send_get_DL_status)
{
    auto& slave = bus.slaves().at(0);
    mock_link->handleProcess(Command::FPRD, uint16_t{0x0530}, 1);

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
    mock_link->handleProcess(Command::FPRD, uint8_t{0}, 0);

    DatagramState state = DatagramState::OK;
    auto error_callback = [&](DatagramState const& s) { state = s; };
    bus.sendGetDLStatus(slave, error_callback);
    bus.processAwaitingFrames();
    ASSERT_EQ(state, DatagramState::INVALID_WKC);
}


TEST_F(BusTest, IRQ_OK)
{
    mock_link->handleWriteThenRead(Command::BWR, 1);
    mock_link->handleWriteThenRead(Command::BWR, 1);
    mock_link->handleWriteThenRead(Command::BWR, 1);
    mock_link->handleWriteThenRead(Command::BWR, 1);

    bus.enableIRQ(EcatEvent::DL_STATUS, [](){});
    bus.enableIRQ(EcatEvent::AL_STATUS, [](){});

    bus.disableIRQ(EcatEvent::DL_STATUS);
    bus.disableIRQ(EcatEvent::AL_STATUS);
}


TEST_F(BusTest, IRQ_NOK)
{
    mock_link->handleWriteThenRead(Command::BWR, 0);
    mock_link->handleWriteThenRead(Command::BWR, 0);

    ASSERT_THROW(bus.enableIRQ(EcatEvent::DL_STATUS, [](){}), kickcat::Error);
    ASSERT_THROW(bus.disableIRQ(EcatEvent::DL_STATUS),        kickcat::Error);
}


TEST_F(BusTest, add_gateway_message)
{
    constexpr uint16_t GATEWAY_INDEX = 42;
    mailbox::request::Mailbox mailbox;
    mailbox.recv_size = 128;

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
    mock_link->handleWriteThenRead(Command::BWR, 0);
    ASSERT_THROW(bus.clearErrorCounters();, kickcat::Error);
}


TEST_F(BusTest, mailbox_sequencer_cycling)
{
    MailboxSequencer sequencer(bus);
    auto noop = [](DatagramState const&){};
    auto& slave = bus.slaves().at(0);

    // Phase 0: sendMailboxesReadChecks -> SM1 empty -> can_read=false
    mock_link->handleProcess(Command::FPRD, uint8_t{0x00}, 1);
    sequencer.step(noop);
    bus.processAwaitingFrames();
    ASSERT_FALSE(slave.mailbox.can_read);

    // Phase 1: sendReadMessages -> nothing queued (can_read=false)
    sequencer.step(noop);

    // Phase 2: sendMailboxesWriteChecks -> SM0 empty -> can_write=true
    mock_link->handleProcess(Command::FPRD, uint8_t{0x00}, 1);
    sequencer.step(noop);
    bus.processAwaitingFrames();
    ASSERT_TRUE(slave.mailbox.can_write);

    // Phase 3: sendWriteMessages -> nothing (no pending messages)
    sequencer.step(noop);

    // Phase 0 again: verify wrap-around -> SM1 full -> can_read=true
    mock_link->handleProcess(Command::FPRD, uint8_t{0x08}, 1);
    sequencer.step(noop);
    bus.processAwaitingFrames();
    ASSERT_TRUE(slave.mailbox.can_read);
}


TEST_F(BusTest, mailbox_sequencer_period)
{
    MailboxSequencer sequencer(bus, 3);
    auto noop = [](DatagramState const&){};

    // Calls 1 and 2: nothing happens (counter < period)
    sequencer.step(noop);
    sequencer.step(noop);

    // Call 3: phase 0 executes (sendMailboxesReadChecks)
    mock_link->handleProcess(Command::FPRD, uint8_t{0x00}, 1);
    sequencer.step(noop);
    bus.processAwaitingFrames();

    // Calls 4 and 5: nothing
    sequencer.step(noop);
    sequencer.step(noop);

    // Call 6: phase 1 executes (sendReadMessages) - nothing queued since can_read=false
    sequencer.step(noop);

    // Calls 7 and 8: nothing
    sequencer.step(noop);
    sequencer.step(noop);

    // Call 9: phase 2 executes (sendMailboxesWriteChecks)
    mock_link->handleProcess(Command::FPRD, uint8_t{0x00}, 1);
    sequencer.step(noop);
    bus.processAwaitingFrames();
}


TEST_F(BusTest, mailbox_sequencer_with_active_read)
{
    MailboxSequencer sequencer(bus);
    auto noop = [](DatagramState const&){};
    auto& slave = bus.slaves().at(0);

    // Phase 0: readCheck -> SM1 full -> can_read=true
    mock_link->handleProcess(Command::FPRD, uint8_t{0x08}, 1);
    sequencer.step(noop);
    bus.processAwaitingFrames();
    ASSERT_TRUE(slave.mailbox.can_read);

    // Phase 1: readMessages -> FPRD expected (because can_read=true)
    mock_link->handleProcess(Command::FPRD, uint8_t{0}, 1);
    sequencer.step(noop);
    bus.processAwaitingFrames();
}


TEST_F(BusTest, mailbox_sequencer_with_active_write)
{
    MailboxSequencer sequencer(bus);
    auto noop = [](DatagramState const&){};
    auto& slave = bus.slaves().at(0);

    // Phase 0: readCheck
    mock_link->handleProcess(Command::FPRD, uint8_t{0x00}, 1);
    sequencer.step(noop);
    bus.processAwaitingFrames();

    // Phase 1: readMessages -> nothing (can_read=false)
    sequencer.step(noop);

    // Phase 2: writeCheck -> SM0 empty -> can_write=true
    mock_link->handleProcess(Command::FPRD, uint8_t{0x00}, 1);
    sequencer.step(noop);
    bus.processAwaitingFrames();
    ASSERT_TRUE(slave.mailbox.can_write);

    // Add a message to send
    int32_t data = 0xCAFEDECA;
    uint32_t data_size = sizeof(data);
    slave.mailbox.createSDO(0x1018, 1, false, CoE::SDO::request::UPLOAD, &data, &data_size, 1s);
    ASSERT_FALSE(slave.mailbox.to_send.empty());

    // Phase 3: writeMessages -> FPWR expected (can_write=true and message pending)
    mock_link->handleProcess(Command::FPWR, uint8_t{0}, 1);
    sequencer.step(noop);
    bus.processAwaitingFrames();
    ASSERT_TRUE(slave.mailbox.to_send.empty());
}


TEST_F(BusTest, writeEeprom_OK)
{
    auto& slave = bus.slaves().at(0);
    uint16_t data = 0xCAFE;

    // big_wait must be non-zero so the acknowledge loop doesn't timeout immediately
    bus.configureWaitLatency(0ns, 1ms);

    // areEepromReady (first call): not busy
    mock_link->handleProcess(Command::FPRD, uint16_t{0x0000}, 1);

    // FPWR EEPROM_DATA
    mock_link->handleProcess(Command::FPWR, uint16_t{0}, 1);

    // FPWR EEPROM_CONTROL (write request)
    mock_link->handleProcess(Command::FPWR, eeprom::Request{}, 1);

    // isEepromAcknowledged: no ERROR_CMD bit
    mock_link->handleProcess(Command::FPRD, uint16_t{0x0000}, 1);

    // areEepromReady (final call): not busy
    mock_link->handleProcess(Command::FPRD, uint16_t{0x0000}, 1);

    ASSERT_NO_THROW(bus.writeEeprom(slave, 0x0010, &data, sizeof(data)));
}


TEST_F(BusTest, writeEeprom_size_too_large)
{
    auto& slave = bus.slaves().at(0);
    uint8_t data[4] = {};

    ASSERT_THROW(bus.writeEeprom(slave, 0x0010, data, sizeof(data)), Error);
}


TEST_F(BusTest, writeEeprom_eeprom_busy_timeout)
{
    auto& slave = bus.slaves().at(0);
    uint16_t data = 0xCAFE;

    // areEepromReady: always busy (10 retries)
    for (int i = 0; i < 10; ++i)
    {
        mock_link->handleProcess(Command::FPRD, uint16_t(eeprom::Control::BUSY), 1);
    }

    ASSERT_THROW(bus.writeEeprom(slave, 0x0010, &data, sizeof(data)), Error);
}


TEST_F(BusTest, writeEeprom_acknowledge_timeout)
{
    auto& slave = bus.slaves().at(0);
    uint16_t data = 0xCAFE;

    // areEepromReady (first call): not busy
    mock_link->handleProcess(Command::FPRD, uint16_t{0x0000}, 1);

    // FPWR EEPROM_DATA
    mock_link->handleProcess(Command::FPWR, uint16_t{0}, 1);

    // Loop: FPWR EEPROM_CONTROL + isEepromAcknowledged with ERROR_CMD set
    // elapsed_time will exceed 10 * big_wait (which is 0ns here) on second iteration
    mock_link->handleProcess(Command::FPWR, eeprom::Request{}, 1);
    mock_link->handleProcess(Command::FPRD, uint16_t(eeprom::Control::ERROR_CMD), 1);

    mock_link->handleProcess(Command::FPWR, eeprom::Request{}, 1);
    mock_link->handleProcess(Command::FPRD, uint16_t(eeprom::Control::ERROR_CMD), 1);

    ASSERT_THROW(bus.writeEeprom(slave, 0x0010, &data, sizeof(data)), Error);
}


TEST_F(BusTest, writeEeprom_data_write_wkc_error)
{
    auto& slave = bus.slaves().at(0);
    uint16_t data = 0xCAFE;

    // areEepromReady: not busy
    mock_link->handleProcess(Command::FPRD, uint16_t{0x0000}, 1);

    // FPWR EEPROM_DATA: wkc=0 -> error
    mock_link->handleProcess(Command::FPWR, uint16_t{0}, 0);

    ASSERT_THROW(bus.writeEeprom(slave, 0x0010, &data, sizeof(data)), Error);
}


// ---------- Mailbox status FMMU tests ----------

TEST_F(BusTest, configureMailboxStatusCheck_read_check_OK)
{
    ASSERT_NO_THROW(bus.configureMailboxStatusCheck(MailboxStatusFMMU::READ_CHECK));
    ASSERT_EQ(MailboxStatusFMMU::READ_CHECK, bus.mailboxStatusFMMUMode());
}

TEST_F(BusTest, configureMailboxStatusCheck_write_check_OK)
{
    ASSERT_NO_THROW(bus.configureMailboxStatusCheck(MailboxStatusFMMU::WRITE_CHECK));
    ASSERT_EQ(MailboxStatusFMMU::WRITE_CHECK, bus.mailboxStatusFMMUMode());
}

TEST_F(BusTest, configureMailboxStatusCheck_both_OK)
{
    auto mode = MailboxStatusFMMU::READ_CHECK | MailboxStatusFMMU::WRITE_CHECK;
    ASSERT_NO_THROW(bus.configureMailboxStatusCheck(mode));
    ASSERT_EQ(mode, bus.mailboxStatusFMMUMode());
}

TEST_F(BusTest, configureMailboxStatusCheck_none)
{
    bus.configureMailboxStatusCheck(MailboxStatusFMMU::READ_CHECK);
    ASSERT_NO_THROW(bus.configureMailboxStatusCheck(MailboxStatusFMMU::NONE));
    ASSERT_EQ(MailboxStatusFMMU::NONE, bus.mailboxStatusFMMUMode());
}

TEST_F(BusTest, configureMailboxStatusCheck_insufficient_fmmus_single)
{
    auto& slave = bus.slaves().at(0);
    slave.esc.fmmus = 2;
    ASSERT_THROW(bus.configureMailboxStatusCheck(MailboxStatusFMMU::READ_CHECK), Error);
    ASSERT_EQ(MailboxStatusFMMU::NONE, bus.mailboxStatusFMMUMode());
}

TEST_F(BusTest, configureMailboxStatusCheck_insufficient_fmmus_both)
{
    auto& slave = bus.slaves().at(0);
    slave.esc.fmmus = 3;
    ASSERT_NO_THROW(bus.configureMailboxStatusCheck(MailboxStatusFMMU::READ_CHECK));
    bus.configureMailboxStatusCheck(MailboxStatusFMMU::NONE);
    ASSERT_THROW(bus.configureMailboxStatusCheck(MailboxStatusFMMU::READ_CHECK | MailboxStatusFMMU::WRITE_CHECK), Error);
}

TEST_F(BusTest, configureMailboxStatusCheck_no_mailbox_slave_skipped)
{
    auto& slave = bus.slaves().at(0);
    slave.sii.supported_mailbox = eeprom::MailboxProtocol::None;
    slave.esc.fmmus = 2;
    ASSERT_NO_THROW(bus.configureMailboxStatusCheck(MailboxStatusFMMU::READ_CHECK | MailboxStatusFMMU::WRITE_CHECK));
}


TEST_F(BusTest, mailbox_status_fmmu_read_check_LRD)
{
    auto& slave = bus.slaves().at(0);
    slave.is_static_mapping = true;
    slave.input.bsize = 4;
    slave.input.sync_manager = 1;
    slave.output.bsize = 4;
    slave.output.sync_manager = 0;

    bus.configureMailboxStatusCheck(MailboxStatusFMMU::READ_CHECK);

    // configureFMMUs: 4 FPWR (SM+FMMU for output & input)
    for (int i = 0; i < 4; ++i)
    {
        mock_link->handleProcess(Command::FPWR, uint8_t{0}, 1);
    }
    // configureMailboxFMMUs: 1 FPWR for FMMU2 (read check)
    mock_link->handleProcess(Command::FPWR, uint8_t{0}, 1);

    uint8_t iomap[64];
    bus.createMapping(iomap);

    // Frame layout: [4 PDO][3 pad][1 status] = 8 bytes
    // Status byte at offset 7, bit 0 = SM1 mailbox status
    // WKC = 1 (input PDO, slave already increments for TxPDO so adjust = 0)

    // LRD with status bit set -> can_read = true
    uint64_t lrd_data = uint64_t(0x01) << 56; // byte 7 = 0x01, bit 0 set
    mock_link->handleProcess(Command::LRD, lrd_data, 1);
    slave.mailbox.can_read = false;
    bus.processDataRead([](DatagramState const&){});
    ASSERT_TRUE(slave.mailbox.can_read);

    // LRD with status bit clear -> can_read = false
    uint64_t lrd_clear = 0;
    mock_link->handleProcess(Command::LRD, lrd_clear, 1);
    bus.processDataRead([](DatagramState const&){});
    ASSERT_FALSE(slave.mailbox.can_read);
}


TEST_F(BusTest, mailbox_status_fmmu_both_LRW)
{
    auto& slave = bus.slaves().at(0);
    slave.is_static_mapping = true;
    slave.input.bsize = 4;
    slave.input.sync_manager = 1;
    slave.output.bsize = 4;
    slave.output.sync_manager = 0;

    bus.configureMailboxStatusCheck(MailboxStatusFMMU::READ_CHECK | MailboxStatusFMMU::WRITE_CHECK);

    // configureFMMUs: 4 FPWR
    for (int i = 0; i < 4; ++i)
    {
        mock_link->handleProcess(Command::FPWR, uint8_t{0}, 1);
    }
    // configureMailboxFMMUs: 2 FPWR (FMMU2 for read + FMMU3 for write)
    mock_link->handleProcess(Command::FPWR, uint8_t{0}, 1);
    mock_link->handleProcess(Command::FPWR, uint8_t{0}, 1);

    uint8_t iomap[64];
    bus.createMapping(iomap);

    // Frame layout: [4 PDO][3 pad][1 read_status][3 pad][1 write_status] = 12 bytes
    // Read status: byte 7, bit 0
    // Write status: byte 11, bit 0
    // LRW WKC = 1 (read) + 1*2 (write) = 3 (slave has TxPDO so adjust = 0)

    struct Response { uint8_t data[12]; } __attribute__((__packed__));
    Response resp{};
    resp.data[7]  = 0x01; // read check: SM1 full -> can_read = true
    resp.data[11] = 0x00; // write check: SM0 empty -> can_write = !0 = true
    mock_link->handleProcess(Command::LRW, resp, 3);

    slave.mailbox.can_read = false;
    slave.mailbox.can_write = false;
    bus.processDataReadWrite([](DatagramState const&){});
    ASSERT_TRUE(slave.mailbox.can_read);
    ASSERT_TRUE(slave.mailbox.can_write);

    // Now test SM0 full -> can_write = false
    Response resp2{};
    resp2.data[7]  = 0x00; // read check: SM1 empty -> can_read = false
    resp2.data[11] = 0x01; // write check: SM0 full -> can_write = !1 = false
    mock_link->handleProcess(Command::LRW, resp2, 3);

    bus.processDataReadWrite([](DatagramState const&){});
    ASSERT_FALSE(slave.mailbox.can_read);
    ASSERT_FALSE(slave.mailbox.can_write);
}


TEST_F(BusTest, mailbox_status_fmmu_LWR_uses_pdo_size)
{
    auto& slave = bus.slaves().at(0);
    slave.is_static_mapping = true;
    slave.input.bsize = 4;
    slave.input.sync_manager = 1;
    slave.output.bsize = 4;
    slave.output.sync_manager = 0;

    bus.configureMailboxStatusCheck(MailboxStatusFMMU::READ_CHECK);

    for (int i = 0; i < 4; ++i)
    {
        mock_link->handleProcess(Command::FPWR, uint8_t{0}, 1);
    }
    mock_link->handleProcess(Command::FPWR, uint8_t{0}, 1);

    uint8_t iomap[64];
    bus.createMapping(iomap);

    // LWR WKC should still be 1 (only output PDO, read FMMUs don't respond to LWR)
    uint32_t lwr_data = 0;
    mock_link->handleProcess(Command::LWR, lwr_data, 1);
    bus.processDataWrite([](DatagramState const&){});
}


TEST_F(BusTest, mailbox_status_fmmu_wkc_error_during_programming)
{
    auto& slave = bus.slaves().at(0);
    slave.is_static_mapping = true;
    slave.input.bsize = 4;
    slave.input.sync_manager = 1;
    slave.output.bsize = 4;
    slave.output.sync_manager = 0;

    bus.configureMailboxStatusCheck(MailboxStatusFMMU::READ_CHECK);

    // configureFMMUs: 4 FPWR OK
    for (int i = 0; i < 4; ++i)
    {
        mock_link->handleProcess(Command::FPWR, uint8_t{0}, 1);
    }
    // configureMailboxFMMUs: FPWR with WKC=0 -> error
    mock_link->handleProcess(Command::FPWR, uint8_t{0}, 0);

    uint8_t iomap[64];
    ASSERT_THROW(bus.createMapping(iomap), Error);
}


TEST_F(BusTest, mailbox_sequencer_skip_fmmu_read_check)
{
    bus.configureMailboxStatusCheck(MailboxStatusFMMU::READ_CHECK);
    MailboxSequencer sequencer(bus);
    auto noop = [](DatagramState const&){};

    // Step 1: phase 0 skipped (FMMU) -> immediately executes sendReadMessages (phase 1)
    sequencer.step(noop);

    // Step 2: sendMailboxesWriteChecks (phase 2, not covered by FMMU)
    mock_link->handleProcess(Command::FPRD, uint8_t{0x00}, 1);
    sequencer.step(noop);
    bus.processAwaitingFrames();

    // Step 3: sendWriteMessages (phase 3)
    sequencer.step(noop);

    // Step 4: phase 0 skipped again -> sendReadMessages (phase 1)
    sequencer.step(noop);
}


TEST_F(BusTest, mailbox_sequencer_skip_fmmu_both)
{
    bus.configureMailboxStatusCheck(MailboxStatusFMMU::READ_CHECK | MailboxStatusFMMU::WRITE_CHECK);
    MailboxSequencer sequencer(bus);
    auto noop = [](DatagramState const&){};

    // Step 1: phase 0 skipped, phase 1 executes -> sendReadMessages
    sequencer.step(noop);

    // Step 2: phase 2 skipped, phase 3 executes -> sendWriteMessages
    sequencer.step(noop);

    // Step 3: phase 0 skipped -> sendReadMessages again (full cycle in 2 calls)
    sequencer.step(noop);
}


TEST_F(BusTest, mailbox_sequencer_no_skip_without_fmmu)
{
    MailboxSequencer sequencer(bus);
    auto noop = [](DatagramState const&){};

    // Phase 0: sendMailboxesReadChecks -> NOT skipped
    mock_link->handleProcess(Command::FPRD, uint8_t{0x00}, 1);
    sequencer.step(noop);
    bus.processAwaitingFrames();

    // Phase 1: sendReadMessages -> nothing
    sequencer.step(noop);

    // Phase 2: sendMailboxesWriteChecks -> NOT skipped
    mock_link->handleProcess(Command::FPRD, uint8_t{0x00}, 1);
    sequencer.step(noop);
    bus.processAwaitingFrames();

    // Phase 3: sendWriteMessages -> nothing
    sequencer.step(noop);
}
