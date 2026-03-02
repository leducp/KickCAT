#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "mocks/ESC.h"

#include "kickcat/Mailbox.h"
#include "kickcat/CoE/mailbox/request.h"
#include "kickcat/CoE/mailbox/response.h"

using namespace kickcat;
using namespace kickcat::mailbox::response;

using ::testing::Return;
using ::testing::_;
using ::testing::Invoke;
using ::testing::DoAll;

constexpr uint16_t RESP_MBX_SIZE     = 256;
constexpr uint16_t RESP_MBX_IN_ADDR  = 0x1000;
constexpr uint16_t RESP_MBX_OUT_ADDR = 0x1200;

static CoE::Dictionary createResponseTestDictionary()
{
    CoE::Dictionary dict;
    CoE::Object object{0x1018, CoE::ObjectCode::ARRAY, "Identity Object", {}};
    CoE::addEntry<uint8_t> (object, 0, 8,  0,  CoE::Access::READ, static_cast<CoE::DataType>(5), "Subindex 000",    0x4);
    CoE::addEntry<uint32_t>(object, 1, 32, 8,  CoE::Access::READ, static_cast<CoE::DataType>(7), "Vendor ID",       0x6a5);
    CoE::addEntry<uint32_t>(object, 2, 32, 40, CoE::Access::READ, static_cast<CoE::DataType>(7), "Product code",    0xb0cad0);
    dict.push_back(std::move(object));
    return dict;
}

class ResponseMailbox : public ::testing::Test
{
public:
    void SetUp() override
    {
        mbx.enableCoE(createResponseTestDictionary());

        EXPECT_CALL(esc, read(reg::SYNC_MANAGER + sizeof(SyncManager) * 0, _, sizeof(SyncManager)))
            .WillRepeatedly(DoAll(
                Invoke([this](uint16_t, void* ptr, uint16_t) { memcpy(ptr, &sm_in, sizeof(SyncManager)); }),
                Return(0)));

        EXPECT_CALL(esc, read(reg::SYNC_MANAGER + sizeof(SyncManager) * 1, _, sizeof(SyncManager)))
            .WillRepeatedly(DoAll(
                Invoke([this](uint16_t, void* ptr, uint16_t) { memcpy(ptr, &sm_out, sizeof(SyncManager)); }),
                Return(0)));

        ASSERT_EQ(0, mbx.configure());
    }

    std::vector<uint8_t> buildRawSDORead(uint16_t index, uint8_t subindex)
    {
        uint32_t data;
        uint32_t data_size = sizeof(data);
        mailbox::request::SDOMessage msg{RESP_MBX_SIZE, index, subindex, false, CoE::SDO::request::UPLOAD, &data, &data_size, 1ms};

        std::vector<uint8_t> raw(RESP_MBX_SIZE, 0);
        std::memcpy(raw.data(), msg.data(), RESP_MBX_SIZE);
        return raw;
    }

    void expectSmStatusRead(uint8_t sm_index, SyncManager const& sync)
    {
        EXPECT_CALL(esc, read(addressSM(sm_index), _, sizeof(SyncManager)))
            .WillOnce(DoAll(
                Invoke([sync](uint16_t, void* ptr, uint16_t) { memcpy(ptr, &sync, sizeof(SyncManager)); }),
                Return(sizeof(SyncManager))))
            .RetiresOnSaturation();
    }

    void expectMailboxDataRead(std::vector<uint8_t> const& raw)
    {
        EXPECT_CALL(esc, read(RESP_MBX_OUT_ADDR, _, RESP_MBX_SIZE))
            .WillOnce(DoAll(
                Invoke([&raw](uint16_t, void* ptr, uint16_t) { memcpy(ptr, raw.data(), RESP_MBX_SIZE); }),
                Return(RESP_MBX_SIZE)))
            .RetiresOnSaturation();
    }

    MockESC esc;
    SyncManager sm_in {RESP_MBX_IN_ADDR,  RESP_MBX_SIZE, SM_CONTROL_MODE_MAILBOX | SM_CONTROL_DIRECTION_READ,  0, SM_ACTIVATE_ENABLE, 0};
    SyncManager sm_out{RESP_MBX_OUT_ADDR, RESP_MBX_SIZE, SM_CONTROL_MODE_MAILBOX | SM_CONTROL_DIRECTION_WRITE, 0, SM_ACTIVATE_ENABLE, 0};
    Mailbox mbx{&esc, RESP_MBX_SIZE, 2};
};


TEST(ResponseMailboxConfigure, not_configured)
{
    MockESC esc;
    Mailbox mbx{&esc, RESP_MBX_SIZE};

    for (uint8_t i = 0; i < reg::SM_STATS; ++i)
    {
        EXPECT_CALL(esc, read(reg::SYNC_MANAGER + sizeof(SyncManager) * i, _, sizeof(SyncManager))).WillOnce(Return(0));
    }
    ASSERT_EQ(-EAGAIN, mbx.configure());
}


TEST(ResponseMailboxConfigure, badly_configured)
{
    MockESC esc;
    Mailbox mbx{&esc, RESP_MBX_SIZE};

    for (int i = 0; i < reg::SM_STATS; ++i)
    {
        EXPECT_CALL(esc, read(reg::SYNC_MANAGER + sizeof(SyncManager) * i, _, sizeof(SyncManager)))
            .WillOnce(Invoke([&](uint16_t, void* data, uint16_t)
            {
                SyncManager sm{};
                std::memset(&sm, 0, sizeof(SyncManager));
                std::memcpy(data, &sm, sizeof(SyncManager));
                return sizeof(SyncManager);
            }));
    }

    ASSERT_EQ(-EAGAIN, mbx.configure());
}


TEST_F(ResponseMailbox, receive_nothing_when_sm_empty)
{
    SyncManager sync{};
    sync.status = 0;
    expectSmStatusRead(1, sync);

    mbx.receive();
}


TEST_F(ResponseMailbox, receive_new_CoE_message)
{
    auto raw = buildRawSDORead(0x1018, 1);

    SyncManager sync{};
    sync.status = SM_STATUS_MAILBOX;
    expectSmStatusRead(1, sync);
    expectMailboxDataRead(raw);

    mbx.receive();
}


TEST_F(ResponseMailbox, receive_read_failure)
{
    SyncManager sync{};
    sync.status = SM_STATUS_MAILBOX;
    expectSmStatusRead(1, sync);

    EXPECT_CALL(esc, read(RESP_MBX_OUT_ADDR, _, RESP_MBX_SIZE))
        .WillOnce(Return(0));

    mbx.receive();
}


TEST_F(ResponseMailbox, receive_unsupported_protocol)
{
    std::vector<uint8_t> raw(RESP_MBX_SIZE, 0);
    auto header = pointData<mailbox::Header>(raw.data());
    header->type = mailbox::Type::VoE;

    SyncManager sync{};
    sync.status = SM_STATUS_MAILBOX;
    expectSmStatusRead(1, sync);
    expectMailboxDataRead(raw);

    mbx.receive();

    auto const& reply = mbx.readyToSend();
    auto resp_header = pointData<mailbox::Header>(reply.data());
    auto err = pointData<mailbox::Error::ServiceData>(resp_header);
    ASSERT_EQ(mailbox::ERR, resp_header->type);
    ASSERT_EQ(mailbox::Error::UNSUPPORTED_PROTOCOL, err->detail);
}


TEST_F(ResponseMailbox, receive_queue_full)
{
    SyncManager sync{};
    sync.status = SM_STATUS_MAILBOX;

    auto raw1 = buildRawSDORead(0x1018, 1);
    expectSmStatusRead(1, sync);
    expectMailboxDataRead(raw1);
    mbx.receive();

    auto raw2 = buildRawSDORead(0x1018, 2);
    expectSmStatusRead(1, sync);
    expectMailboxDataRead(raw2);
    mbx.receive();

    // Third receive should be rejected (queue full, max_msgs=2)
    auto raw3 = buildRawSDORead(0x1018, 1);
    expectSmStatusRead(1, sync);
    expectMailboxDataRead(raw3);
    mbx.receive();

    auto const& reply = mbx.readyToSend();
    auto header = pointData<mailbox::Header>(reply.data());
    auto err = pointData<mailbox::Error::ServiceData>(header);
    ASSERT_EQ(mailbox::ERR, header->type);
    ASSERT_EQ(mailbox::Error::NO_MORE_MEMORY, err->detail);
}


TEST_F(ResponseMailbox, process_finalize_message)
{
    auto raw = buildRawSDORead(0x1018, 1);

    SyncManager sync{};
    sync.status = SM_STATUS_MAILBOX;
    expectSmStatusRead(1, sync);
    expectMailboxDataRead(raw);

    mbx.receive();
    mbx.process();

    auto const& reply = mbx.readyToSend();
    auto header = pointData<mailbox::Header>(reply.data());
    auto coe = pointData<CoE::Header>(header);
    auto sdo = pointData<CoE::ServiceData>(coe);
    auto payload = pointData<uint32_t>(sdo);

    ASSERT_EQ(CoE::Service::SDO_RESPONSE, coe->service);
    ASSERT_EQ(0x6a5, *payload);
}


TEST_F(ResponseMailbox, process_empty_queue)
{
    mbx.process();
}


TEST_F(ResponseMailbox, send_nothing_when_queue_empty)
{
    SyncManager sync{};
    sync.status = 0;
    sync.activate = 0;
    sync.pdi_control = 0;
    expectSmStatusRead(0, sync);

    mbx.send();
}


TEST_F(ResponseMailbox, send_message)
{
    auto raw = buildRawSDORead(0x1018, 2);

    SyncManager sync_out{};
    sync_out.status = SM_STATUS_MAILBOX;
    expectSmStatusRead(1, sync_out);
    expectMailboxDataRead(raw);

    mbx.receive();
    mbx.process();

    SyncManager sync_in{};
    sync_in.status = 0;
    sync_in.activate = 0;
    sync_in.pdi_control = 0;
    expectSmStatusRead(0, sync_in);

    EXPECT_CALL(esc, write(RESP_MBX_IN_ADDR, _, RESP_MBX_SIZE))
        .WillOnce(Return(RESP_MBX_SIZE));

    mbx.send();
}


TEST_F(ResponseMailbox, send_blocked_when_mailbox_full)
{
    auto raw = buildRawSDORead(0x1018, 2);

    SyncManager sync_out{};
    sync_out.status = SM_STATUS_MAILBOX;
    expectSmStatusRead(1, sync_out);
    expectMailboxDataRead(raw);

    mbx.receive();
    mbx.process();

    SyncManager sync_in{};
    sync_in.status = SM_STATUS_MAILBOX;
    sync_in.activate = 0;
    sync_in.pdi_control = 0;
    expectSmStatusRead(0, sync_in);

    EXPECT_CALL(esc, write(RESP_MBX_IN_ADDR, _, _)).Times(0);

    mbx.send();
}


TEST_F(ResponseMailbox, send_repeat_procedure)
{
    auto raw = buildRawSDORead(0x1018, 2);

    SyncManager sync_out{};
    sync_out.status = SM_STATUS_MAILBOX;
    expectSmStatusRead(1, sync_out);
    expectMailboxDataRead(raw);

    mbx.receive();
    mbx.process();

    // First send: normal
    SyncManager sync_first{};
    sync_first.status = 0;
    sync_first.activate = 0;
    sync_first.pdi_control = 0;
    expectSmStatusRead(0, sync_first);

    EXPECT_CALL(esc, write(RESP_MBX_IN_ADDR, _, RESP_MBX_SIZE))
        .WillOnce(Return(RESP_MBX_SIZE))
        .RetiresOnSaturation();

    mbx.send();

    // Second send: IRQ_READ + repeat requested
    SyncManager sync_repeat{};
    sync_repeat.status = SM_STATUS_IRQ_READ;
    sync_repeat.activate = SM_ACTIVATE_REPEAT_REQ;
    sync_repeat.pdi_control = 0;
    expectSmStatusRead(0, sync_repeat);

    // IRQ reset write
    EXPECT_CALL(esc, write(RESP_MBX_IN_ADDR, _, 1))
        .WillOnce(Return(1))
        .RetiresOnSaturation();

    // Repeat message write
    EXPECT_CALL(esc, write(RESP_MBX_IN_ADDR, _, RESP_MBX_SIZE))
        .WillOnce(Return(RESP_MBX_SIZE))
        .RetiresOnSaturation();

    // Ack repeat
    EXPECT_CALL(esc, write(addressSM(0) + 7, _, sizeof(uint8_t)))
        .WillOnce(Return(sizeof(uint8_t)));

    mbx.send();
}


TEST_F(ResponseMailbox, full_receive_process_send_cycle)
{
    auto raw = buildRawSDORead(0x1018, 2);

    SyncManager sync_out{};
    sync_out.status = SM_STATUS_MAILBOX;
    expectSmStatusRead(1, sync_out);
    expectMailboxDataRead(raw);
    mbx.receive();

    mbx.process();

    SyncManager sync_in{};
    sync_in.status = 0;
    sync_in.activate = 0;
    sync_in.pdi_control = 0;
    expectSmStatusRead(0, sync_in);

    std::vector<uint8_t> sent_data(RESP_MBX_SIZE, 0);
    EXPECT_CALL(esc, write(RESP_MBX_IN_ADDR, _, RESP_MBX_SIZE))
        .WillOnce(DoAll(
            Invoke([&sent_data](uint16_t, void const* ptr, uint16_t size) { memcpy(sent_data.data(), ptr, size); }),
            Return(RESP_MBX_SIZE)));

    mbx.send();

    auto header = pointData<mailbox::Header>(sent_data.data());
    auto coe = pointData<CoE::Header>(header);
    auto sdo = pointData<CoE::ServiceData>(coe);
    auto payload = pointData<uint32_t>(sdo);

    ASSERT_EQ(CoE::Service::SDO_RESPONSE, coe->service);
    ASSERT_EQ(0xb0cad0, *payload);
}
