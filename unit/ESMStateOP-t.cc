#include "ESMStateTest.h"
#include "kickcat/protocol.h"


class ESMStateOPTest : public ESMStateTest
{
public:
    Context context = Context::build(State::OPERATIONAL);

    void SetUpSpecific() override
    {
        expectSyncManagerRead(0, mbx_in);
        expectSyncManagerRead(1, mbx_out);
        expectSyncManagerRead(2, pdo_in);
        expectSyncManagerRead(3, pdo_out);

        // In safeop mbx and pdo sync managers need to be configured
        mbx_->configureSm();
        pdo_->configure_pdo_sm();

        // Pdo input and output expected to be updated
        expectUpdatePdoInput();
        expectUpdatePdoOutput();

        context.al_watchdog_process_data = 0x1;
    }
};

TEST_F(ESMStateOPTest, 37_OP_to_Init)
{
    Context newContext = op->routine(context, ALControl{State::INIT});

    expectAlStatus(newContext, State::INIT);

    expectSyncManagerActivate(0, false);
    expectSyncManagerActivate(1, false);
    expectSyncManagerActivate(2, false);
    expectSyncManagerActivate(3, false);
    init->on_entry(context, newContext);
}

TEST_F(ESMStateOPTest, 38_OP_to_PreOP)
{
    Context newContext = op->routine(context, ALControl{State::PRE_OP});

    expectAlStatus(newContext, State::PRE_OP);

    expectSyncManagerActivate(0, true);
    expectSyncManagerActivate(1, true);
    expectSyncManagerActivate(2, false);
    expectSyncManagerActivate(3, false);
    preop->on_entry(context, newContext);
}

TEST_F(ESMStateOPTest, 39_OP_to_SafeOP)
{
    Context newContext = op->routine(context, ALControl{State::SAFE_OP});

    expectAlStatus(newContext, State::SAFE_OP);

    expectSyncManagerActivate(2, true);
    expectSyncManagerActivate(3, true);
    safeop->on_entry(context, newContext);
}

TEST_F(ESMStateOPTest, 40_OP_to_OP)
{
    Context newContext = op->routine(context, ALControl{State::OPERATIONAL});

    expectAlStatus(newContext, State::OPERATIONAL);
}

TEST_F(ESMStateOPTest, 42_OP_to_SafeOP)
{
    Context newContext = op->routine(context, ALControl{State::BOOT});

    expectAlStatus(newContext, State::SAFE_OP, StatusCode::INVALID_REQUESTED_STATE_CHANGE);

    expectSyncManagerActivate(3, false);
    safeop->on_entry(context, newContext);
}

TEST_F(ESMStateOPTest, 43_OP_to_SafeOP)
{
    Context newContext = op->routine(context, ALControl{UNKNOWN_STATE});

    expectAlStatus(newContext, State::SAFE_OP, StatusCode::UNKNOWN_REQUESTED_STATE);

    expectSyncManagerActivate(3, false);
    safeop->on_entry(context, newContext);
}

TEST_F(ESMStateOPTest, 45A_OP_to_PreOP)
{
    pdo_in.activate = ~SM_ACTIVATE_ENABLE;

    Context newContext = op->routine(context, ALControl{State::OPERATIONAL});

    expectAlStatus(newContext, State::PRE_OP, StatusCode::INVALID_INPUT_CONFIGURATION);
}

TEST_F(ESMStateOPTest, 45B_OP_to_PreOP)
{
    pdo_out.activate = ~SM_ACTIVATE_ENABLE;

    Context newContext = op->routine(context, ALControl{State::OPERATIONAL});

    expectAlStatus(newContext, State::PRE_OP, StatusCode::INVALID_OUTPUT_CONFIGURATION);
}

TEST_F(ESMStateOPTest, 46A_OP_to_Init)
{
    mbx_out.activate = ~SM_ACTIVATE_ENABLE;

    Context newContext = op->routine(context, ALControl{State::OPERATIONAL});

    expectAlStatus(newContext, State::INIT, StatusCode::INVALID_MAILBOX_CONFIGURATION_PREOP);
}

TEST_F(ESMStateOPTest, 46B_OP_to_Init)
{
    mbx_out.activate = ~SM_ACTIVATE_ENABLE;

    Context newContext = op->routine(context, ALControl{State::OPERATIONAL});

    expectAlStatus(newContext, State::INIT, StatusCode::INVALID_MAILBOX_CONFIGURATION_PREOP);
}

TEST_F(ESMStateOPTest, 49_OP_to_SafeOP)
{
    context.al_watchdog_process_data = 0;

    Context newContext = op->routine(context, ALControl{State::OPERATIONAL});

    expectAlStatus(newContext, State::SAFE_OP, StatusCode::SYNC_MANAGER_WATCHDOG);
}

