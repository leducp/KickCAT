#include "mocks/ESMStateTest.h"

using namespace kickcat;
using namespace kickcat::ESM;
using namespace testing;

class ESMStateInitTest : public ESMStateTest
{
};

TEST_F(ESMStateInitTest, 1_1_Init_to_Init)
{
    Context newContext =
        init.routine(Context::build(State::INIT, StatusCode::INVALID_REQUESTED_STATE_CHANGE), ALControl{State::INIT});

    expectAlStatus(newContext, State::INIT);
}

TEST_F(ESMStateInitTest, 1_2_Init_to_Init)
{
    for (auto requestedState :
         std::list<uint16_t>{State::PRE_OP, State::BOOT, State::SAFE_OP, State::OPERATIONAL, UNKNOWN_STATE})
    {
        Context newContext = init.routine(Context::build(State::INIT, StatusCode::INVALID_REQUESTED_STATE_CHANGE),
                                           ALControl{requestedState});

        expectAlStatus(newContext, State::INIT, StatusCode::INVALID_REQUESTED_STATE_CHANGE);
    }
}

TEST_F(ESMStateInitTest, 2A_Init_to_Init)
{
    Context newContext = init.routine(Context::build(State::INIT), ALControl{State::INIT});

    expectAlStatus(newContext, State::INIT);
}

TEST_F(ESMStateInitTest, 2B_ErrInit_to_Init)
{
    Context newContext = init.routine(Context::build(State::INIT, StatusCode::INVALID_REQUESTED_STATE_CHANGE),
                                       ALControl{State::INIT | State::ERROR_ACK});

    EXPECT_EQ(newContext.al_status, State::INIT);
    EXPECT_EQ(newContext.al_status_code, StatusCode::NO_ERROR);
}

TEST_F(ESMStateInitTest, 3A_Init_to_PreOP)
{
    expectSyncManagerRead(0, mbx_in);
    expectSyncManagerRead(1, mbx_out);

    Context newContext = init.routine(Context::build(State::INIT), ALControl{State::PRE_OP});

    expectAlStatus(newContext, State::PRE_OP);

    expectSyncManagerActivate(0);
    expectSyncManagerActivate(1);

    preop.onEntry(Context::build(State::INIT), newContext);
}

TEST_F(ESMStateInitTest, 3A_Init_to_PreOP_no_mbx)
{
    init.setMailbox(nullptr);

    Context newContext = init.routine(Context::build(State::INIT), ALControl{State::PRE_OP});

    expectAlStatus(newContext, State::PRE_OP);

    preop.onEntry(Context::build(State::INIT), newContext);
}


TEST_F(ESMStateInitTest, 3B_ErrInit_to_PreOP)
{
    expectSyncManagerRead(0, mbx_in);
    expectSyncManagerRead(1, mbx_out);

    Context newContext = init.routine(Context::build(State::INIT, StatusCode::INVALID_REQUESTED_STATE_CHANGE),
                                       ALControl{State::PRE_OP | State::ERROR_ACK});

    expectAlStatus(newContext, State::PRE_OP);

    expectSyncManagerActivate(0);
    expectSyncManagerActivate(1);

    preop.onEntry(Context::build(State::INIT), newContext);
}

TEST_F(ESMStateInitTest, 4A_Init_to_Init_sm_not_match)
{
    mbx_in.activate = ~SM_ACTIVATE_ENABLE;
    expectSyncManagerRead(0, mbx_in);
    expectSyncManagerRead(1, mbx_out);

    Context newContext = init.routine(Context::build(State::INIT), ALControl{State::PRE_OP});

    expectAlStatus(newContext, State::INIT, StatusCode::INVALID_MAILBOX_CONFIGURATION_PREOP);
}

TEST_F(ESMStateInitTest, 4B_ErrInit_to_Init_sm_not_match)
{
    mbx_in.activate = ~SM_ACTIVATE_ENABLE;
    expectSyncManagerRead(0, mbx_in);
    expectSyncManagerRead(1, mbx_out);

    Context newContext = init.routine(Context::build(State::INIT, StatusCode::INVALID_REQUESTED_STATE_CHANGE),
                                       ALControl{State::PRE_OP | State::ERROR_ACK});

    expectAlStatus(newContext, State::INIT, StatusCode::INVALID_MAILBOX_CONFIGURATION_PREOP);
}

TEST_F(ESMStateInitTest, 7_Init_to_Init_Boot_not_supported)
{
    Context newContext = init.routine(Context::build(State::INIT), ALControl{State::BOOT});

    expectAlStatus(newContext, State::INIT, StatusCode::BOOTSTRAP_NOT_SUPPORTED);
}

TEST_F(ESMStateInitTest, 7_ErrInit_to_Init_Boot_not_supported)
{
    Context newContext = init.routine(Context::build(State::INIT, StatusCode::INVALID_REQUESTED_STATE_CHANGE),
                                       ALControl{State::BOOT | State::ERROR_ACK});

    expectAlStatus(newContext, State::INIT, StatusCode::BOOTSTRAP_NOT_SUPPORTED);
}

TEST_F(ESMStateInitTest, 8A_Init_to_Init_safeop_requested)
{
    Context newContext = init.routine(Context::build(State::INIT), ALControl{State::SAFE_OP});

    expectAlStatus(newContext, State::INIT, StatusCode::INVALID_REQUESTED_STATE_CHANGE);
}

TEST_F(ESMStateInitTest, 8B_ErrInit_to_Init_safeop_requested)
{
    Context newContext = init.routine(Context::build(State::INIT, StatusCode::INVALID_MAILBOX_CONFIGURATION_PREOP),
                                       ALControl{State::SAFE_OP | State::ERROR_ACK});

    expectAlStatus(newContext, State::INIT, StatusCode::INVALID_REQUESTED_STATE_CHANGE);
}

TEST_F(ESMStateInitTest, 8C_Init_to_Init_op_requested)
{
    Context newContext = init.routine(Context::build(State::INIT), ALControl{State::OPERATIONAL});

    expectAlStatus(newContext, State::INIT, StatusCode::INVALID_REQUESTED_STATE_CHANGE);
}

TEST_F(ESMStateInitTest, 8D_ErrInit_to_Init_eop_requested)
{
    Context newContext = init.routine(Context::build(State::INIT, StatusCode::INVALID_MAILBOX_CONFIGURATION_PREOP),
                                       ALControl{State::OPERATIONAL | State::ERROR_ACK});

    expectAlStatus(newContext, State::INIT, StatusCode::INVALID_REQUESTED_STATE_CHANGE);
}

TEST_F(ESMStateInitTest, 9A_Init_to_Init_UnkownState)
{
    Context newContext = init.routine(Context::build(State::INIT), ALControl{UNKNOWN_STATE});

    expectAlStatus(newContext, State::INIT, StatusCode::UNKNOWN_REQUESTED_STATE);
}

TEST_F(ESMStateInitTest, 9B_ErrInit_to_Init_UknownState)
{
    Context newContext = init.routine(Context::build(State::INIT, StatusCode::INVALID_REQUESTED_STATE_CHANGE),
                                       ALControl{static_cast<uint16_t>(UNKNOWN_STATE | State::ERROR_ACK)});

    expectAlStatus(newContext, State::INIT, StatusCode::UNKNOWN_REQUESTED_STATE);
}
