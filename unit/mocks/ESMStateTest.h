#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include "kickcat/ESM.h"
#include "kickcat/ESMStates.h"
#include "kickcat/Mailbox.h"
#include "kickcat/protocol.h"

using namespace kickcat;
using namespace kickcat::ESM;
using namespace testing;

class MockESC : public kickcat::AbstractESC
{
public:
    MOCK_METHOD(int32_t, read, (uint16_t address, void* data, uint16_t size), (override));
    MOCK_METHOD(int32_t, write, (uint16_t address, void const* data, uint16_t size), (override));
    MOCK_METHOD(int32_t, init, (), (override));
};

class ESMStateTest : public testing::Test
{
public:
    uint16_t UNKNOWN_STATE{0xF};
    MockESC esc_{};
    PDO pdo_{&esc_};
    mailbox::response::Mailbox mbx_{&esc_, 200};
    uint8_t buffer_in_[1024];
    uint8_t buffer_out_[1024];

    SyncManager mbx_in;
    SyncManager mbx_out;
    SyncManager pdo_in;
    SyncManager pdo_out;

    Init init{esc_, pdo_};
    PreOP preop{esc_, pdo_};
    SafeOP safeop{esc_, pdo_};
    OP op{esc_, pdo_};

    virtual void SetUpSpecific()
    {
    }

    void SetUp() override
    {
        init.setMailbox(&mbx_);
        preop.setMailbox(&mbx_);
        safeop.setMailbox(&mbx_);
        op.setMailbox(&mbx_);

        mbx_in  = {0x00, 100, SM_CONTROL_MODE_MAILBOX | SM_CONTROL_DIRECTION_READ, 0x00, SM_ACTIVATE_ENABLE, 0x00};
        mbx_out = {0x01, 100, SM_CONTROL_MODE_MAILBOX | SM_CONTROL_DIRECTION_WRITE, 0x00, SM_ACTIVATE_ENABLE, 0x00};
        pdo_in  = {0x02, 200, SM_CONTROL_MODE_BUFFERED | SM_CONTROL_DIRECTION_READ, 0x00, SM_ACTIVATE_ENABLE, 0x00};
        pdo_out = {0x03, 200, SM_CONTROL_MODE_BUFFERED | SM_CONTROL_DIRECTION_WRITE, 0x00, SM_ACTIVATE_ENABLE, 0x00};

        pdo_.setInput(buffer_in_);
        pdo_.setOutput(buffer_out_);

        SetUpSpecific();
    }

    void expectAlStatus(Context context, State state, StatusCode statusCode = StatusCode::NO_ERROR)
    {
        if (statusCode == StatusCode::NO_ERROR)
        {
            EXPECT_EQ(context.al_status, state);
        }
        else
        {
            EXPECT_EQ(context.al_status, state | ERROR_ACK);
        }

        EXPECT_EQ(context.al_status_code, statusCode);
    }

    void expectSyncManagerRead(uint8_t index, SyncManager& syncManager)
    {
        EXPECT_CALL(esc_, read(reg::SYNC_MANAGER + sizeof(SyncManager) * index, _, sizeof(SyncManager)))
            .WillRepeatedly(DoAll(
                testing::Invoke([&](uint16_t, void* ptr, uint16_t) { memcpy(ptr, &syncManager, sizeof(SyncManager)); }),
                Return(0)));
    }

    void expectUpdatePdoInput()
    {
        EXPECT_CALL(esc_, write(pdo_in.start_address, _, pdo_in.length)).WillOnce(Return(0));
    }

    void expectUpdatePdoOutput()
    {
        EXPECT_CALL(esc_, read(pdo_out.start_address, _, pdo_out.length)).WillOnce(Return(0));
    }

    void expectSyncManagerActivate(uint8_t index, bool enable = true)
    {
        EXPECT_CALL(esc_, read(reg::SYNC_MANAGER + sizeof(SyncManager) * index + 7, _, sizeof(uint8_t)))
            .WillRepeatedly(Return(0));

        EXPECT_CALL(esc_, write(reg::SYNC_MANAGER + sizeof(SyncManager) * index + 7, _, sizeof(uint8_t)))
            .WillOnce(Return(0));

        uint8_t pdi_control = 0;
        if (not enable)
        {
            pdi_control = 1;
        }

        EXPECT_CALL(esc_, read(reg::SYNC_MANAGER + sizeof(SyncManager) * index + 7, _, 1))
            .WillRepeatedly(DoAll(
                testing::Invoke([=](uint16_t, void* ptr, uint16_t) { memcpy(ptr, &pdi_control, sizeof(uint8_t)); }),
                Return(0)));
    }
};
