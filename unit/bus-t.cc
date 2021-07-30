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
    void handleReply(std::vector<T> answers, uint16_t replied_wkc = 1)
    {
        EXPECT_CALL(*io, read(_,_))
        .WillOnce(Invoke([this, replied_wkc, answers](uint8_t* data, int32_t)
        {
            auto it = answers.begin();
            uint16_t* wkc = reinterpret_cast<uint16_t*>(payload + header->len);
            DatagramHeader* current_header = header;                     // current heade rto check loop condition

            do
            {
                std::memcpy(payload, &(*it), sizeof(T));
                *wkc = replied_wkc;

                current_header = header;                                    // save current header
                ++it;                                                       // next payload
                datagram = reinterpret_cast<uint8_t*>(wkc) + 2;             // next datagram
                header = reinterpret_cast<DatagramHeader*>(datagram);       // next header
                payload = datagram + sizeof(DatagramHeader);                // next payload
                wkc = reinterpret_cast<uint16_t*>(payload + header->len);   // next wkc
            } while (current_header->multiple == 1);

            int32_t answer_size = inflight.finalize();
            std::memcpy(data, inflight.data(), answer_size);
            return answer_size;
        }));
    }

    void handleReply(uint16_t wkc = 1)
    {
        handleReply<uint8_t>({0}, wkc);
    }

    void addFetchEepromWord(uint32_t word)
    {
        // address
        checkSendFrame(Command::BWR);
        handleReply();

        // eeprom ready
        checkSendFrame(Command::FPRD);
        handleReply<uint16_t>({0x0080}); // 0x8000 in LE

        // fetch reply
        checkSendFrame(Command::FPRD);
        handleReply<uint32_t>({word});
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
        handleReply<uint8_t>({State::INIT});

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
        checkSendFrame(Command::FPWR);
        handleReply();

        // request state: PREOP
        checkSendFrame(Command::BWR);
        handleReply();

        // check state
        checkSendFrame(Command::FPRD);
        handleReply<uint8_t>({State::PRE_OP});

        // clear mailbox
        checkSendFrame(Command::FPRD);
        handleReply<uint8_t>({0x08, 0}); // can write, nothing to read

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
    handleReply<ErrorCounters>({counters});

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

    auto& slave = bus.slaves().at(0);
    slave.supported_mailbox = eeprom::MailboxProtocol::None; // disable mailbox protocol to use SII PDO mapping

    checkSendFrame(Command::FPWR);
    handleReply<uint8_t>({2, 3});

    uint8_t iomap[64];
    bus.createMapping(iomap);

    // note: bit size is round up
    ASSERT_EQ(32,  slave.input.bsize);
    ASSERT_EQ(255, slave.input.size);
    ASSERT_EQ(48,  slave.output.bsize);
    ASSERT_EQ(383, slave.output.size);

    // test logical read/write/read and write

    int64_t logical_read = 0x0001020304050607;
    checkSendFrame(Command::LRD);
    handleReply<int64_t>({logical_read});
    bus.processDataRead([](){});

    for (int i = 0; i < 8; ++i)
    {
        ASSERT_EQ(7 - i, slave.input.data[i]);
    }

    int64_t logical_write = 0x0706050403020100;
    std::memcpy(slave.output.data, &logical_write, sizeof(int64_t));
    checkSendFrame(Command::LWR, logical_write);
    handleReply<int64_t>({logical_write});
    bus.processDataWrite([](){});

    for (int i = 0; i < 8; ++i)
    {
        ASSERT_EQ(7 - i, slave.input.data[i]);
    }

    logical_read  = 0x1011121314151617;
    logical_write = 0x1716151413121110;
    std::memcpy(slave.output.data, &logical_write, sizeof(int64_t));
    checkSendFrame(Command::LRW, logical_write);
    handleReply<int64_t>({logical_read});
    bus.processDataReadWrite([](){});

    for (int i = 0; i < 8; ++i)
    {
        ASSERT_EQ(0x17 - i, slave.input.data[i]);
    }

    // check error callbacks
    checkSendFrame(Command::LRD);
    handleReply(0);
    ASSERT_THROW(bus.processDataRead([](){ throw std::out_of_range(""); }), std::out_of_range);

    checkSendFrame(Command::LWR);
    handleReply(0);
    ASSERT_THROW(bus.processDataWrite([](){ throw std::logic_error(""); }), std::logic_error);

    checkSendFrame(Command::LRW);
    handleReply(0);
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
    handleReply<Feedback>({al});

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
    handleReply<Feedback>({al}, 0);
    bus.getCurrentState(slave);
    ASSERT_EQ(State::INVALID, slave.al_status);

    checkSendFrame(Command::FPRD);
    al.status = 0x1;
    handleReply<Feedback>({al});
    ASSERT_THROW(bus.waitForState(State::OPERATIONAL, 0ns), Error);

    checkSendFrame(Command::BWR);
    handleReply(0);
    ASSERT_THROW(bus.requestState(State::INIT), Error);
}


TEST_F(BusTest, messages_errors)
{
    auto& slave = bus.slaves().at(0);
    slave.mailbox.can_read = true;

    checkSendFrame(Command::FPRD);
    handleReply(0);
    bus.sendReadMessages([](){ throw std::logic_error(""); });
    ASSERT_THROW(bus.processAwaitingFrames(), std::logic_error);

    checkSendFrame(Command::FPRD);
    handleReply();
    bus.sendReadMessages([](){ throw std::out_of_range(""); });
    ASSERT_THROW(bus.processAwaitingFrames(), std::out_of_range);
}


TEST_F(BusTest, write_SDO_OK)
{
    InSequence s;

    int32_t data = 0xCAFEDECA;
    uint32_t data_size = sizeof(data);
    auto& slave = bus.slaves().at(0);

    checkSendFrame(Command::FPRD);
    handleReply<uint8_t>({0x08, 0});// cannot write, nothing to read

    checkSendFrame(Command::FPRD);
    handleReply<uint8_t>({0, 0});   // can write, nothing to read

    checkSendFrame(Command::FPWR);  // write to mailbox
    handleReply();

    checkSendFrame(Command::FPRD);
    handleReply<uint8_t>({0, 0});   // can write, nothing to read

    checkSendFrame(Command::FPRD);
    handleReply<uint8_t>({0, 0x08});// can write, somethin to read

    // answer
    struct Answer
    {
        mailbox::Header header;
        mailbox::ServiceData sdo;
    } __attribute__((__packed__));
    Answer answer;
    answer.header.len = 10;
    answer.header.type = mailbox::Type::CoE;
    answer.sdo.service = CoE::Service::SDO_RESPONSE;
    answer.sdo.command = CoE::SDO::response::DOWNLOAD;
    answer.sdo.index = 0x1018;
    answer.sdo.subindex = 1;

    checkSendFrame(Command::FPRD);
    handleReply<Answer>({answer}); // read answer

    bus.writeSDO(slave, 0x1018, 1, false, &data, data_size);
}

TEST_F(BusTest, write_SDO_timeout)
{
    InSequence s;

    int32_t data = 0xCAFEDECA;
    uint32_t data_size = sizeof(data);
    auto& slave = bus.slaves().at(0);

    checkSendFrame(Command::FPRD);
    handleReply<uint8_t>({0x08, 0});// cannot write, nothing to read

    ASSERT_THROW(bus.writeSDO(slave, 0x1018, 1, false, &data, data_size, 0ns), Error);
}

TEST_F(BusTest, write_SDO_bad_answer)
{
    InSequence s;

    int32_t data = 0xCAFEDECA;
    uint32_t data_size = sizeof(data);
    auto& slave = bus.slaves().at(0);

    checkSendFrame(Command::FPRD);
    handleReply<uint8_t>({0, 0});// can write, nothing to read

    checkSendFrame(Command::FPWR);  // write to mailbox
    handleReply(0);

    ASSERT_THROW(bus.writeSDO(slave, 0x1018, 1, false, &data, data_size), Error);
}

TEST_F(BusTest, read_SDO_OK)
{
    InSequence s;

    int32_t data = 0;
    uint32_t data_size = sizeof(data);
    auto& slave = bus.slaves().at(0);

    checkSendFrame(Command::FPRD);
    handleReply<uint8_t>({0, 0});// can write, nothing to read

    checkSendFrame(Command::FPWR);  // write to mailbox
    handleReply();

    checkSendFrame(Command::FPRD);
    handleReply<uint8_t>({0, 0x08});// can write, somethin to read

    // answer
    struct Answer
    {
        mailbox::Header header;
        mailbox::ServiceData sdo;
        int32_t payload;
    } __attribute__((__packed__));
    Answer answer;
    answer.header.len = 10;
    answer.header.type = mailbox::Type::CoE;
    answer.sdo.service = CoE::Service::SDO_RESPONSE;
    answer.sdo.command = CoE::SDO::response::UPLOAD;
    answer.sdo.index = 0x1018;
    answer.sdo.subindex = 1;
    answer.sdo.transfer_type = 1;
    answer.sdo.block_size = 0;
    answer.payload = 0xDEADBEEF;

    checkSendFrame(Command::FPRD);
    handleReply<Answer>({answer}); // read answer

    bus.readSDO(slave, 0x1018, 1, Bus::Access::PARTIAL, &data, &data_size);
    ASSERT_EQ(0xDEADBEEF, data);
    ASSERT_EQ(4, data_size);
}

TEST_F(BusTest, read_SDO_emulated_complete_access_OK)
{
    InSequence s;

    // answer
    struct Answer
    {
        mailbox::Header header;
        mailbox::ServiceData sdo;
        uint32_t payload;
    } __attribute__((__packed__));
    Answer answer;
    answer.header.len = 10;
    answer.header.type = mailbox::Type::CoE;
    answer.sdo.service = CoE::Service::SDO_RESPONSE;
    answer.sdo.command = CoE::SDO::response::UPLOAD;
    answer.sdo.index = 0x1018;
    answer.sdo.transfer_type = 1;

    uint32_t payload_array[4] = { 3, 0xCAFE0000, 0x0000DECA, 0xFADEFACE };

    uint32_t data[3] = {0};
    uint32_t data_size = sizeof(data);
    auto& slave = bus.slaves().at(0);

    for (int i = 0; i < 4; ++i)
    {
        checkSendFrame(Command::FPRD);
        handleReply<uint8_t>({0, 0});// can write, nothing to read

        checkSendFrame(Command::FPWR);  // write to mailbox
        handleReply();

        checkSendFrame(Command::FPRD);
        handleReply<uint8_t>({0, 0x08});// can write, something to read

        answer.sdo.subindex = i;
        answer.sdo.block_size = 0;
        answer.payload = payload_array[i];
        checkSendFrame(Command::FPRD);
        handleReply<Answer>({answer}); // read answer
    }

    bus.readSDO(slave, 0x1018, 1, Bus::Access::EMULATE_COMPLETE, &data, &data_size);
    ASSERT_EQ(0xCAFE0000, data[0]);
    ASSERT_EQ(0x0000DECA, data[1]);
    ASSERT_EQ(0xFADEFACE, data[2]);
    ASSERT_EQ(12, data_size);
}


TEST_F(BusTest, read_SDO_buffer_too_small)
{
    InSequence s;

    // answer
    struct Answer
    {
        mailbox::Header header;
        mailbox::ServiceData sdo;
        uint32_t payload;
    } __attribute__((__packed__));
    Answer answer;
    answer.header.len = 10;
    answer.header.type = mailbox::Type::CoE;
    answer.sdo.service = CoE::Service::SDO_RESPONSE;
    answer.sdo.command = CoE::SDO::response::UPLOAD;
    answer.sdo.index = 0x1018;
    answer.sdo.transfer_type = 1;

    uint32_t payload_array[2] = { 3, 0xCAFE0000 };

    uint32_t data = {0};
    uint32_t data_size = sizeof(data);
    auto& slave = bus.slaves().at(0);

    for (int i = 0; i < 2; ++i)
    {
        checkSendFrame(Command::FPRD);
        handleReply<uint8_t>({0, 0});// can write, nothing to read

        checkSendFrame(Command::FPWR);  // write to mailbox
        handleReply();

        checkSendFrame(Command::FPRD);
        handleReply<uint8_t>({0, 0x08});// can write, something to read

        answer.sdo.subindex = i;
        answer.sdo.block_size = 0;
        answer.payload = payload_array[i];
        checkSendFrame(Command::FPRD);
        handleReply<Answer>({answer}); // read answer
    }

    ASSERT_THROW(bus.readSDO(slave, 0x1018, 1, Bus::Access::EMULATE_COMPLETE, &data, &data_size), Error);
}
