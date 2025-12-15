#include "mocks/ESC.h"

#include "kickcat/slave/Slave.h"

using namespace kickcat;
using namespace kickcat::slave;

using ::testing::AtLeast;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::_;

class SlaveTest : public testing::Test
{
public:
    MockESC esc_;
    PDO pdo_{&esc_};
    Slave slave_{&esc_, &pdo_};
    mailbox::response::Mailbox mbx_{&esc_, 256, 1};

    void SetUp() override
    {
        uint16_t const al_control = 0x01;
        EXPECT_CALL(esc_, read(reg::AL_CONTROL, _, sizeof(uint16_t)))
            .WillOnce(Invoke([al_control](uint16_t, void* data, uint16_t)
                {
                    std::memcpy(data, &al_control, sizeof(uint16_t));
                    return sizeof(uint16_t);
                }));

        uint16_t const wdog_status = 0;
        EXPECT_CALL(esc_, read(reg::WDOG_STATUS, _, sizeof(uint16_t)))
            .WillOnce(Invoke([wdog_status](uint16_t, void* data, uint16_t)
            {
                std::memcpy(data, &wdog_status, sizeof(uint16_t));
                return sizeof(uint16_t);
            }));
    }
};

TEST_F(SlaveTest, routine_without_mailbox)
{
    slave_.start();
    slave_.routine();
    ASSERT_EQ(slave_.state(), State::INIT);
}

TEST_F(SlaveTest, routine_with_mailbox)
{
    slave_.setMailbox(&mbx_);
    slave_.start();

    // When mailbox is not configured, both receive() and send() use index 0
    SyncManager sm{};
    sm.status = 0; // no message, mailbox empty
    EXPECT_CALL(esc_, read(addressSM(0), _, sizeof(SyncManager)))
        .Times(AtLeast(1))
        .WillRepeatedly(Invoke([&](uint16_t, void* data, uint16_t)
        {
            std::memcpy(data, &sm, sizeof(SyncManager));
            return sizeof(SyncManager);
        }));

    slave_.routine();
    ASSERT_EQ(slave_.state(), State::INIT);
}


