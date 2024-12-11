#include "ESMStateTest.h"
#include "kickcat/protocol.h"


class ESMStatePreOPTest : public ESMStateTest
{
    void SetUpSpecific() override
    {
        expectSyncManagerRead(0, mbx_in);
        expectSyncManagerRead(1, mbx_out);

        // In Preop mbx sync managers need to be configured
        mbx_->configureSm();
    }
};

TEST_F(ESMStatePreOPTest, 11_1_ErrPreOP_to_Init)
{
    Context newContext = preop->routine(Context::build(State::PRE_OP, StatusCode::INVALID_REQUESTED_STATE_CHANGE),
                                        ALControl{State::INIT});

    expectAlStatus(newContext, State::INIT);

    expectSyncManagerActivate(0, false);
    expectSyncManagerActivate(1, false);
    init->on_entry(Context::build(State::PRE_OP), newContext);
}
TEST_F(ESMStatePreOPTest, 11_2_ErrPreOP_to_ErrPreOP)
{
    for (auto requestedState :
         std::list<uint16_t>{State::PRE_OP, State::BOOT, State::SAFE_OP, State::OPERATIONAL, UNKNOWN_STATE})
    {
        Context newContext = preop->routine(Context::build(State::PRE_OP, StatusCode::INVALID_REQUESTED_STATE_CHANGE),
                                            ALControl{requestedState});

        expectAlStatus(newContext, State::PRE_OP, StatusCode::INVALID_REQUESTED_STATE_CHANGE);
    }
}

TEST_F(ESMStatePreOPTest, 12A_PreOP_to_PreOP)
{
    Context newContext = preop->routine(Context::build(State::PRE_OP), ALControl{State::INIT});

    expectAlStatus(newContext, State::INIT);
}

TEST_F(ESMStatePreOPTest, 12B_ErrPreOP_to_PreOP)
{
    Context newContext = preop->routine(Context::build(State::PRE_OP, StatusCode::INVALID_REQUESTED_STATE_CHANGE),
                                        ALControl{State::INIT | State::ERROR_ACK});

    expectAlStatus(newContext, State::INIT);
}

TEST_F(ESMStatePreOPTest, 13A_PreOP_to_PreOP)
{
    Context newContext = preop->routine(Context::build(State::PRE_OP), ALControl{State::PRE_OP});

    expectAlStatus(newContext, State::PRE_OP);
}

TEST_F(ESMStatePreOPTest, 13B_ErrPreOP_to_PreOP)
{
    Context newContext = preop->routine(Context::build(State::PRE_OP, StatusCode::INVALID_REQUESTED_STATE_CHANGE),
                                        ALControl{State::PRE_OP | State::ERROR_ACK});

    expectAlStatus(newContext, State::PRE_OP);
}

TEST_F(ESMStatePreOPTest, 14_1A_PreOP_to_Safeop)
{
    expectSyncManagerRead(2, pdo_in);
    expectSyncManagerRead(3, pdo_out);

    Context newContext = preop->routine(Context::build(State::PRE_OP), ALControl{State::SAFE_OP});

    expectAlStatus(newContext, State::SAFE_OP);

    expectSyncManagerActivate(2);
    expectSyncManagerActivate(3);
    safeop->on_entry(Context::build(State::PRE_OP), newContext);
}

TEST_F(ESMStatePreOPTest, 14_1B_ErrPreOP_to_Safeop)
{
    expectSyncManagerRead(2, pdo_in);
    expectSyncManagerRead(3, pdo_out);

    Context newContext = preop->routine(Context::build(State::PRE_OP, StatusCode::INVALID_REQUESTED_STATE_CHANGE),
                                        ALControl{State::SAFE_OP | State::ERROR_ACK});

    expectAlStatus(newContext, State::SAFE_OP);

    expectSyncManagerActivate(2);
    expectSyncManagerActivate(3);
    safeop->on_entry(Context::build(State::PRE_OP), newContext);
}

TEST_F(ESMStatePreOPTest, 17A_PreOP_to_ErrPreOP)
{
    pdo_in.activate = ~SM_ACTIVATE_ENABLE;
    expectSyncManagerRead(2, pdo_in);
    expectSyncManagerRead(3, pdo_out);

    Context newContext = preop->routine(Context::build(State::PRE_OP), ALControl{State::SAFE_OP});

    expectAlStatus(newContext, State::PRE_OP, StatusCode::INVALID_INPUT_CONFIGURATION);
}

TEST_F(ESMStatePreOPTest, 17B_ErrPreOP_to_ErrPreOP)
{
    pdo_in.activate = ~SM_ACTIVATE_ENABLE;
    expectSyncManagerRead(2, pdo_in);
    expectSyncManagerRead(3, pdo_out);

    Context newContext = preop->routine(Context::build(State::PRE_OP, StatusCode::INVALID_REQUESTED_STATE_CHANGE),
                                        ALControl{State::SAFE_OP | State::ERROR_ACK});

    expectAlStatus(newContext, State::PRE_OP, StatusCode::INVALID_INPUT_CONFIGURATION);
}

TEST_F(ESMStatePreOPTest, 17C_PreOP_to_ErrPreOP)
{
    pdo_out.activate = ~SM_ACTIVATE_ENABLE;
    expectSyncManagerRead(2, pdo_in);
    expectSyncManagerRead(3, pdo_out);

    Context newContext = preop->routine(Context::build(State::PRE_OP), ALControl{State::SAFE_OP});

    expectAlStatus(newContext, State::PRE_OP, StatusCode::INVALID_OUTPUT_CONFIGURATION);
}

TEST_F(ESMStatePreOPTest, 17D_PreOP_to_ErrPreOP)
{
    pdo_out.activate = ~SM_ACTIVATE_ENABLE;
    expectSyncManagerRead(2, pdo_in);
    expectSyncManagerRead(3, pdo_out);

    Context newContext = preop->routine(Context::build(State::PRE_OP, StatusCode::INVALID_REQUESTED_STATE_CHANGE),
                                        ALControl{State::SAFE_OP | State::ERROR_ACK});

    expectAlStatus(newContext, State::PRE_OP, StatusCode::INVALID_OUTPUT_CONFIGURATION);
}

TEST_F(ESMStatePreOPTest, 18A_PreOP_to_ErrPreOP)
{
    Context newContext = preop->routine(Context::build(State::PRE_OP), ALControl{State::BOOT});

    expectAlStatus(newContext, State::PRE_OP, StatusCode::INVALID_REQUESTED_STATE_CHANGE);
}

TEST_F(ESMStatePreOPTest, 18B_ErrPreOP_to_ErrPreOP)
{
    Context newContext = preop->routine(Context::build(State::PRE_OP, StatusCode::INVALID_INPUT_CONFIGURATION),
                                        ALControl{State::BOOT | State::ERROR_ACK});

    expectAlStatus(newContext, State::PRE_OP, StatusCode::INVALID_REQUESTED_STATE_CHANGE);
}

TEST_F(ESMStatePreOPTest, 19A_PreOP_to_ErrPreOP)
{
    Context newContext = preop->routine(Context::build(State::PRE_OP), ALControl{UNKNOWN_STATE});

    expectAlStatus(newContext, State::PRE_OP, StatusCode::UNKNOWN_REQUESTED_STATE);
}

TEST_F(ESMStatePreOPTest, 19B_ErrPreOP_to_ErrPreOP)
{
    Context newContext = preop->routine(Context::build(State::PRE_OP, StatusCode::INVALID_REQUESTED_STATE_CHANGE),
                                        ALControl{static_cast<uint16_t>(UNKNOWN_STATE | State::ERROR_ACK)});

    expectAlStatus(newContext, State::PRE_OP, StatusCode::UNKNOWN_REQUESTED_STATE);
}

TEST_F(ESMStatePreOPTest, 20_2_ErrPreOP_to_ErrPreOP)
{
    pdo_out.activate = ~SM_ACTIVATE_ENABLE;

    Context newContext = preop->routine(Context::build(State::PRE_OP, StatusCode::INVALID_REQUESTED_STATE_CHANGE),
                                        ALControl{static_cast<uint16_t>(State::PRE_OP)});

    expectAlStatus(newContext, State::PRE_OP, StatusCode::INVALID_REQUESTED_STATE_CHANGE);
}

TEST_F(ESMStatePreOPTest, 21_PreOP_to_Init)
{
    mbx_in.activate = ~SM_ACTIVATE_ENABLE;

    Context newContext = preop->routine(Context::build(State::PRE_OP), ALControl{static_cast<uint16_t>(State::PRE_OP)});

    expectAlStatus(newContext, State::INIT, StatusCode::INVALID_MAILBOX_CONFIGURATION_PREOP);

    expectSyncManagerActivate(0, false);
    expectSyncManagerActivate(1, false);
    init->on_entry(Context::build(State::PRE_OP), newContext);
}
