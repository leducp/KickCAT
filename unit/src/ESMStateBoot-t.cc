#include "mocks/ESMStateTest.h"

using namespace kickcat;
using namespace kickcat::ESM;
using namespace testing;

class ESMStateBootTest : public ESMStateTest
{
public:
    SyncManager::Register boot_mbx_in{0x1000, 128, SM_CONTROL_MODE_MAILBOX | SM_CONTROL_DIRECTION_READ, 0x00, SM_ACTIVATE_ENABLE, 0x00};
    SyncManager::Register boot_mbx_out{0x1400, 128, SM_CONTROL_MODE_MAILBOX | SM_CONTROL_DIRECTION_WRITE, 0x00, SM_ACTIVATE_ENABLE, 0x00};

    void SetUpSpecific() override
    {
        init.setBootstrapMailbox(&boot_mbx_);
        boot.setBootstrapMailbox(&boot_mbx_);
    }

    // Enter BOOT from INIT, with the bootstrap mailbox configured by the master
    void enterBoot()
    {
        expectSyncManagerRead(0, boot_mbx_in);
        expectSyncManagerRead(1, boot_mbx_out);
        Context newContext = init.routine(Context::build(State::INIT), ALControl{State::BOOT});
        expectAlStatus(newContext, State::BOOT);
    }
};

TEST_F(ESMStateBootTest, Init_to_Boot)
{
    enterBoot();

    expectSyncManagerActivate(0);
    expectSyncManagerActivate(1);
    boot.onEntry(Context::build(State::INIT), Context::build(State::BOOT));
}

TEST_F(ESMStateBootTest, Init_to_Boot_invalid_mailbox)
{
    boot_mbx_out.length = 8;    // too small for any message
    expectSyncManagerRead(0, boot_mbx_in);
    expectSyncManagerRead(1, boot_mbx_out);

    Context newContext = init.routine(Context::build(State::INIT), ALControl{State::BOOT});
    expectAlStatus(newContext, State::INIT, StatusCode::INVALID_MAILBOX_CONFIGURATION_BOOT);
}

TEST_F(ESMStateBootTest, Init_to_Boot_not_supported)
{
    init.setBootstrapMailbox(nullptr);

    Context newContext = init.routine(Context::build(State::INIT), ALControl{State::BOOT});
    expectAlStatus(newContext, State::INIT, StatusCode::BOOTSTRAP_NOT_SUPPORTED);
}

TEST_F(ESMStateBootTest, Boot_to_Boot)
{
    enterBoot();

    Context newContext = boot.routine(Context::build(State::BOOT), ALControl{State::BOOT});
    expectAlStatus(newContext, State::BOOT);
}

TEST_F(ESMStateBootTest, Boot_to_Init)
{
    enterBoot();

    Context newContext = boot.routine(Context::build(State::BOOT), ALControl{State::INIT});
    expectAlStatus(newContext, State::INIT);

    expectSyncManagerActivate(0, false);
    expectSyncManagerActivate(1, false);
    init.onEntry(Context::build(State::BOOT), newContext);
}

TEST_F(ESMStateBootTest, Boot_to_ErrInit_invalid_request)
{
    enterBoot();

    for (auto requested : std::list<uint16_t>{State::PRE_OP, State::SAFE_OP, State::OPERATIONAL})
    {
        Context newContext = boot.routine(Context::build(State::BOOT), ALControl{requested});
        expectAlStatus(newContext, State::INIT, StatusCode::INVALID_REQUESTED_STATE_CHANGE);
    }
}

TEST_F(ESMStateBootTest, Boot_to_ErrInit_unknown_request)
{
    enterBoot();

    Context newContext = boot.routine(Context::build(State::BOOT), ALControl{UNKNOWN_STATE});
    expectAlStatus(newContext, State::INIT, StatusCode::UNKNOWN_REQUESTED_STATE);
}

TEST_F(ESMStateBootTest, ErrBoot_ignores_request_until_acknowledged)
{
    enterBoot();

    for (auto requested : std::list<uint16_t>{State::PRE_OP, UNKNOWN_STATE})
    {
        Context newContext = boot.routine(Context::build(State::BOOT, StatusCode::INVALID_REQUESTED_STATE_CHANGE),
                                          ALControl{requested});
        expectAlStatus(newContext, State::BOOT, StatusCode::INVALID_REQUESTED_STATE_CHANGE);
    }

    Context newContext = boot.routine(Context::build(State::BOOT, StatusCode::INVALID_REQUESTED_STATE_CHANGE),
                                      ALControl{State::INIT});
    expectAlStatus(newContext, State::INIT);
}

TEST_F(ESMStateBootTest, Boot_mailbox_changed)
{
    enterBoot();

    boot_mbx_in.start_address = 0x1800;
    Context newContext = boot.routine(Context::build(State::BOOT), ALControl{State::BOOT});
    expectAlStatus(newContext, State::INIT, StatusCode::INVALID_MAILBOX_CONFIGURATION_PREOP);
}
