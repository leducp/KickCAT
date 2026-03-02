#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <cstring>

#include "mocks/ESC.h"
#include "kickcat/slave/Slave.h"
#include "kickcat/Mailbox.h"
#include "kickcat/CoE/OD.h"
#include "kickcat/protocol.h"

using namespace kickcat;
using namespace kickcat::slave;
using namespace testing;

constexpr uint16_t MBX_SIZE     = 256;
constexpr uint16_t MBX_IN_ADDR  = 0x1000;
constexpr uint16_t MBX_OUT_ADDR = 0x1200;
constexpr uint16_t PDO_IN_ADDR  = 0x1400;
constexpr uint16_t PDO_OUT_ADDR = 0x1600;

static CoE::Dictionary createTestDictionary()
{
    CoE::Dictionary dict;

    CoE::Object object{0x2000, CoE::ObjectCode::VAR, "Test Object", {}};
    CoE::addEntry<uint32_t>(object, 0, 32, 0, CoE::Access::READ | CoE::Access::WRITE,
                            CoE::DataType::UNSIGNED32, "Test Value", uint32_t{0xDEADBEEF});
    dict.push_back(std::move(object));

    CoE::Object object2{0x2001, CoE::ObjectCode::RECORD, "Multi Subindex", {}};
    CoE::addEntry<uint8_t>(object2, 0, 8, 0, CoE::Access::READ,
                           CoE::DataType::UNSIGNED8, "Max Subindex", uint8_t{2});
    CoE::addEntry<uint16_t>(object2, 1, 16, 8, CoE::Access::READ | CoE::Access::WRITE,
                            CoE::DataType::UNSIGNED16, "Sub 1", uint16_t{0xCAFE});
    CoE::addEntry<uint16_t>(object2, 2, 16, 24, CoE::Access::READ | CoE::Access::WRITE,
                            CoE::DataType::UNSIGNED16, "Sub 2", uint16_t{0xBEEF});
    dict.push_back(std::move(object2));

    return dict;
}


class SlaveTest : public ::testing::Test
{
public:
    NiceMock<MockESC> esc_{};
    PDO pdo_{&esc_};
    Slave slave_{&esc_, &pdo_};

    uint8_t buffer_in_[256]{};
    uint8_t buffer_out_[256]{};

    SyncManager mbx_in_ {MBX_IN_ADDR,  MBX_SIZE, SM_CONTROL_MODE_MAILBOX | SM_CONTROL_DIRECTION_READ,  0, SM_ACTIVATE_ENABLE, 0};
    SyncManager mbx_out_{MBX_OUT_ADDR, MBX_SIZE, SM_CONTROL_MODE_MAILBOX | SM_CONTROL_DIRECTION_WRITE, 0, SM_ACTIVATE_ENABLE, 0};
    SyncManager pdo_in_ {PDO_IN_ADDR,  sizeof(buffer_in_),  SM_CONTROL_MODE_BUFFERED | SM_CONTROL_DIRECTION_READ,  0, SM_ACTIVATE_ENABLE, 0};
    SyncManager pdo_out_{PDO_OUT_ADDR, sizeof(buffer_out_), SM_CONTROL_MODE_BUFFERED | SM_CONTROL_DIRECTION_WRITE, 0, SM_ACTIVATE_ENABLE, 0};

    mailbox::response::Mailbox mbx_{&esc_, MBX_SIZE};

    uint16_t al_control_{State::INIT};

    void SetUp() override
    {
        pdo_.setInput(buffer_in_, sizeof(buffer_in_));
        pdo_.setOutput(buffer_out_, sizeof(buffer_out_));

        ON_CALL(esc_, read(reg::AL_CONTROL, _, sizeof(uint16_t)))
            .WillByDefault(DoAll(
                Invoke([this](uint16_t, void* ptr, uint16_t)
                { std::memcpy(ptr, &al_control_, sizeof(uint16_t)); }),
                Return(0)));

        uint16_t wdog_ok = 0x1;
        ON_CALL(esc_, read(reg::WDOG_STATUS, _, sizeof(uint16_t)))
            .WillByDefault(DoAll(
                Invoke([wdog_ok](uint16_t, void* ptr, uint16_t)
                { std::memcpy(ptr, &wdog_ok, sizeof(uint16_t)); }),
                Return(0)));

        setupSmDefaults();
    }

    void setupSmDefaults()
    {
        ON_CALL(esc_, read(reg::SYNC_MANAGER + sizeof(SyncManager) * 0, _, sizeof(SyncManager)))
            .WillByDefault(DoAll(
                Invoke([this](uint16_t, void* ptr, uint16_t)
                { std::memcpy(ptr, &mbx_in_, sizeof(SyncManager)); }),
                Return(0)));

        ON_CALL(esc_, read(reg::SYNC_MANAGER + sizeof(SyncManager) * 1, _, sizeof(SyncManager)))
            .WillByDefault(DoAll(
                Invoke([this](uint16_t, void* ptr, uint16_t)
                { std::memcpy(ptr, &mbx_out_, sizeof(SyncManager)); }),
                Return(0)));

        ON_CALL(esc_, read(reg::SYNC_MANAGER + sizeof(SyncManager) * 2, _, sizeof(SyncManager)))
            .WillByDefault(DoAll(
                Invoke([this](uint16_t, void* ptr, uint16_t)
                { std::memcpy(ptr, &pdo_in_, sizeof(SyncManager)); }),
                Return(0)));

        ON_CALL(esc_, read(reg::SYNC_MANAGER + sizeof(SyncManager) * 3, _, sizeof(SyncManager)))
            .WillByDefault(DoAll(
                Invoke([this](uint16_t, void* ptr, uint16_t)
                { std::memcpy(ptr, &pdo_out_, sizeof(SyncManager)); }),
                Return(0)));

        SyncManager sm_empty{};
        ON_CALL(esc_, read(reg::SYNC_MANAGER + sizeof(SyncManager) * 4, _, sizeof(SyncManager)))
            .WillByDefault(DoAll(
                Invoke([sm_empty](uint16_t, void* ptr, uint16_t)
                { std::memcpy(ptr, &sm_empty, sizeof(SyncManager)); }),
                Return(0)));
    }

    void configureMailbox()
    {
        mbx_.enableCoE(createTestDictionary());
        ASSERT_EQ(0, mbx_.configure());
        slave_.setMailbox(&mbx_);
    }

    void configurePdo()
    {
        ASSERT_EQ(0, pdo_.configure());
    }

    void goToPreOP()
    {
        al_control_ = State::PRE_OP;
        slave_.routine();
        ASSERT_EQ(State::PRE_OP, slave_.state());
    }

    void goToSafeOP()
    {
        goToPreOP();
        configurePdo();
        al_control_ = State::SAFE_OP;
        slave_.routine();
        ASSERT_EQ(State::SAFE_OP, slave_.state());
    }

    void goToOP()
    {
        goToSafeOP();
        slave_.validateOutputData();
        al_control_ = State::OPERATIONAL;
        slave_.routine();
        ASSERT_EQ(State::OPERATIONAL, slave_.state());
    }
};

TEST_F(SlaveTest, start_enters_init_state)
{
    ASSERT_EQ(State::INIT, slave_.state());
    slave_.start();
    ASSERT_EQ(State::INIT, slave_.state());
}


// --- Routine orchestration ---

TEST_F(SlaveTest, routine_without_mailbox_plays_state_machine)
{
    slave_.start();
    al_control_ = State::INIT;

    EXPECT_CALL(esc_, read(reg::AL_CONTROL, _, _)).Times(AtLeast(1));
    EXPECT_CALL(esc_, read(reg::WDOG_STATUS, _, _)).Times(AtLeast(1));

    slave_.routine();
    ASSERT_EQ(State::INIT, slave_.state());
}


TEST_F(SlaveTest, routine_with_mailbox_stays_in_state)
{
    configureMailbox();
    slave_.start();
    al_control_ = State::INIT;

    slave_.routine();
    ASSERT_EQ(State::INIT, slave_.state());
}


TEST_F(SlaveTest, routine_with_mailbox_transitions)
{
    configureMailbox();
    slave_.start();

    al_control_ = State::PRE_OP;
    slave_.routine();
    ASSERT_EQ(State::PRE_OP, slave_.state());

    al_control_ = State::INIT;
    slave_.routine();
    ASSERT_EQ(State::INIT, slave_.state());
}


TEST_F(SlaveTest, multiple_routines_no_spurious_transitions)
{
    slave_.start();
    al_control_ = State::INIT;

    for (int i = 0; i < 10; ++i)
    {
        slave_.routine();
        ASSERT_EQ(State::INIT, slave_.state());
    }
}


// --- State transitions ---

TEST_F(SlaveTest, transition_init_to_preop_without_mailbox)
{
    slave_.start();
    al_control_ = State::PRE_OP;
    slave_.routine();
    ASSERT_EQ(State::PRE_OP, slave_.state());
}


TEST_F(SlaveTest, transition_init_to_preop_with_mailbox)
{
    configureMailbox();
    slave_.start();
    al_control_ = State::PRE_OP;
    slave_.routine();
    ASSERT_EQ(State::PRE_OP, slave_.state());
}


TEST_F(SlaveTest, transition_preop_to_init)
{
    slave_.start();
    goToPreOP();

    al_control_ = State::INIT;
    slave_.routine();
    ASSERT_EQ(State::INIT, slave_.state());
}


TEST_F(SlaveTest, transition_preop_to_safeop)
{
    slave_.start();
    goToPreOP();
    configurePdo();

    al_control_ = State::SAFE_OP;
    slave_.routine();
    ASSERT_EQ(State::SAFE_OP, slave_.state());
}


TEST_F(SlaveTest, transition_safeop_to_preop)
{
    slave_.start();
    goToSafeOP();

    al_control_ = State::PRE_OP;
    slave_.routine();
    ASSERT_EQ(State::PRE_OP, slave_.state());
}


TEST_F(SlaveTest, transition_safeop_to_init)
{
    slave_.start();
    goToSafeOP();

    al_control_ = State::INIT;
    slave_.routine();
    ASSERT_EQ(State::INIT, slave_.state());
}


TEST_F(SlaveTest, transition_safeop_to_op_requires_validateOutputData)
{
    slave_.start();
    goToSafeOP();

    al_control_ = State::OPERATIONAL;
    slave_.routine();
    ASSERT_EQ(State::SAFE_OP, slave_.state());

    slave_.validateOutputData();
    slave_.routine();
    ASSERT_EQ(State::OPERATIONAL, slave_.state());
}


TEST_F(SlaveTest, transition_op_to_safeop)
{
    slave_.start();
    goToOP();

    al_control_ = State::SAFE_OP;
    slave_.routine();
    ASSERT_EQ(State::SAFE_OP, slave_.state());
}


TEST_F(SlaveTest, transition_op_to_preop)
{
    slave_.start();
    goToOP();

    al_control_ = State::PRE_OP;
    slave_.routine();
    ASSERT_EQ(State::PRE_OP, slave_.state());
}


TEST_F(SlaveTest, transition_op_to_init)
{
    slave_.start();
    goToOP();

    al_control_ = State::INIT;
    slave_.routine();
    ASSERT_EQ(State::INIT, slave_.state());
}


// --- Invalid transitions ---

TEST_F(SlaveTest, invalid_transition_init_to_op)
{
    slave_.start();
    al_control_ = State::OPERATIONAL;
    slave_.routine();
    ASSERT_EQ(State::INIT, slave_.state());
}


TEST_F(SlaveTest, invalid_transition_init_to_safeop)
{
    slave_.start();
    al_control_ = State::SAFE_OP;
    slave_.routine();
    ASSERT_EQ(State::INIT, slave_.state());
}


TEST_F(SlaveTest, invalid_transition_preop_to_op)
{
    slave_.start();
    goToPreOP();

    al_control_ = State::OPERATIONAL;
    slave_.routine();
    ASSERT_EQ(State::PRE_OP, slave_.state());
}


// --- Full lifecycle ---

TEST_F(SlaveTest, full_lifecycle)
{
    slave_.start();
    ASSERT_EQ(State::INIT, slave_.state());

    goToPreOP();
    configurePdo();

    al_control_ = State::SAFE_OP;
    slave_.routine();
    ASSERT_EQ(State::SAFE_OP, slave_.state());

    al_control_ = State::OPERATIONAL;
    slave_.routine();
    ASSERT_EQ(State::SAFE_OP, slave_.state());

    slave_.validateOutputData();
    slave_.routine();
    ASSERT_EQ(State::OPERATIONAL, slave_.state());

    al_control_ = State::INIT;
    slave_.routine();
    ASSERT_EQ(State::INIT, slave_.state());
}


TEST_F(SlaveTest, full_lifecycle_with_mailbox)
{
    configureMailbox();
    slave_.start();
    ASSERT_EQ(State::INIT, slave_.state());

    goToPreOP();
    configurePdo();

    al_control_ = State::SAFE_OP;
    slave_.routine();
    ASSERT_EQ(State::SAFE_OP, slave_.state());

    slave_.validateOutputData();
    al_control_ = State::OPERATIONAL;
    slave_.routine();
    ASSERT_EQ(State::OPERATIONAL, slave_.state());

    al_control_ = State::SAFE_OP;
    slave_.routine();
    ASSERT_EQ(State::SAFE_OP, slave_.state());

    al_control_ = State::INIT;
    slave_.routine();
    ASSERT_EQ(State::INIT, slave_.state());
}


// --- bind() ---

TEST_F(SlaveTest, bind_resolves_existing_entry)
{
    configureMailbox();

    uint32_t* bound_ptr = nullptr;
    slave_.bind(0x2000, bound_ptr, 0);

    ASSERT_NE(nullptr, bound_ptr);
    ASSERT_EQ(0xDEADBEEF, *bound_ptr);
}


TEST_F(SlaveTest, bind_resolves_specific_subindex)
{
    configureMailbox();

    uint16_t* sub1 = nullptr;
    slave_.bind(0x2001, sub1, 1);
    ASSERT_NE(nullptr, sub1);
    ASSERT_EQ(0xCAFE, *sub1);

    uint16_t* sub2 = nullptr;
    slave_.bind(0x2001, sub2, 2);
    ASSERT_NE(nullptr, sub2);
    ASSERT_EQ(0xBEEF, *sub2);
}


TEST_F(SlaveTest, bind_unknown_index)
{
    configureMailbox();

    uint32_t* bound_ptr = nullptr;
    slave_.bind(0x9999, bound_ptr, 0);
    ASSERT_EQ(nullptr, bound_ptr);
}


TEST_F(SlaveTest, bind_wrong_subindex)
{
    configureMailbox();

    uint32_t* bound_ptr = nullptr;
    slave_.bind(0x2000, bound_ptr, 42);
    ASSERT_EQ(nullptr, bound_ptr);
}


TEST_F(SlaveTest, bind_without_mailbox)
{
    uint32_t* bound_ptr = nullptr;
    slave_.bind(0x2000, bound_ptr, 0);
    ASSERT_EQ(nullptr, bound_ptr);
}


TEST_F(SlaveTest, bind_returns_writable_pointer)
{
    configureMailbox();

    uint32_t* ptr = nullptr;
    slave_.bind(0x2000, ptr, 0);
    ASSERT_NE(nullptr, ptr);

    *ptr = 0x12345678;

    uint32_t* ptr2 = nullptr;
    slave_.bind(0x2000, ptr2, 0);
    ASSERT_EQ(ptr, ptr2);
    ASSERT_EQ(0x12345678, *ptr2);
}


TEST_F(SlaveTest, bind_different_objects_return_different_pointers)
{
    configureMailbox();

    uint32_t* ptr_a = nullptr;
    slave_.bind(0x2000, ptr_a, 0);

    uint16_t* ptr_b = nullptr;
    slave_.bind(0x2001, ptr_b, 1);

    ASSERT_NE(nullptr, ptr_a);
    ASSERT_NE(nullptr, ptr_b);
    ASSERT_NE(static_cast<void*>(ptr_a), static_cast<void*>(ptr_b));
}
