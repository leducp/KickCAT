#include "mocks/ESMStateTest.h"

class ESMStateSafeOPTest : public ESMStateTest
{
public:
    void SetUpSpecific() override
    {
        expectSyncManagerRead(0, mbx_in);
        expectSyncManagerRead(1, mbx_out);
        expectSyncManagerRead(2, pdo_in);
        expectSyncManagerRead(3, pdo_out);

        // In safeop mbx and pdo sync managers need to be configured
        mbx_.configure();
        pdo_.configure();
    }
};

TEST_F(ESMStateSafeOPTest, 22_1_SafeOP_to_Init)
{
    Context newContext = safeop.routine(Context::build(State::SAFE_OP, StatusCode::INVALID_REQUESTED_STATE_CHANGE),
                                         ALControl{static_cast<uint16_t>(State::INIT)});

    expectAlStatus(newContext, State::INIT);

    expectSyncManagerActivate(0, false);
    expectSyncManagerActivate(1, false);
    expectSyncManagerActivate(2, false);
    expectSyncManagerActivate(3, false);
    init.onEntry(Context::build(State::SAFE_OP, StatusCode::INVALID_REQUESTED_STATE_CHANGE), newContext);
}

TEST_F(ESMStateSafeOPTest, 22_2_ErrSafeOP_to_ErrSafeOP)
{
    for (auto requestedState :
         std::list<uint16_t>{State::PRE_OP, State::BOOT, State::SAFE_OP, State::OPERATIONAL, UNKNOWN_STATE})
    {
        Context newContext = safeop.routine(Context::build(State::SAFE_OP, StatusCode::INVALID_REQUESTED_STATE_CHANGE),
                                             ALControl{requestedState});

        expectAlStatus(newContext, State::SAFE_OP, StatusCode::INVALID_REQUESTED_STATE_CHANGE);
    }
}

TEST_F(ESMStateSafeOPTest, 23A_SafeOP_to_Init)
{
    expectUpdatePdoInput();
    expectUpdatePdoOutput();

    Context newContext = safeop.routine(Context::build(State::SAFE_OP), ALControl{State::INIT});

    expectAlStatus(newContext, State::INIT);

    expectSyncManagerActivate(0, false);
    expectSyncManagerActivate(1, false);
    expectSyncManagerActivate(2, false);
    expectSyncManagerActivate(3, false);
    init.onEntry(Context::build(State::SAFE_OP), newContext);
}

TEST_F(ESMStateSafeOPTest, 23B_ErrSafeOP_to_Init)
{
    expectUpdatePdoInput();
    expectUpdatePdoOutput();

    Context newContext = safeop.routine(Context::build(State::SAFE_OP, StatusCode::INVALID_REQUESTED_STATE_CHANGE),
                                         ALControl{State::INIT | State::ERROR_ACK});

    expectAlStatus(newContext, State::INIT);

    expectSyncManagerActivate(0, false);
    expectSyncManagerActivate(1, false);
    expectSyncManagerActivate(2, false);
    expectSyncManagerActivate(3, false);
    init.onEntry(Context::build(State::SAFE_OP), newContext);
}

TEST_F(ESMStateSafeOPTest, 24A_SafeOP_to_PreOP)
{
    expectUpdatePdoInput();
    expectUpdatePdoOutput();

    Context newContext = safeop.routine(Context::build(State::SAFE_OP), ALControl{State::PRE_OP});

    expectAlStatus(newContext, State::PRE_OP);

    expectSyncManagerActivate(0, true);
    expectSyncManagerActivate(1, true);
    expectSyncManagerActivate(2, false);
    expectSyncManagerActivate(3, false);
    preop.onEntry(Context::build(State::SAFE_OP), newContext);
}

TEST_F(ESMStateSafeOPTest, 24B_ErrSafeOP_to_PreOP)
{
    expectUpdatePdoInput();
    expectUpdatePdoOutput();

    Context newContext = safeop.routine(Context::build(State::SAFE_OP, StatusCode::INVALID_REQUESTED_STATE_CHANGE),
                                         ALControl{State::PRE_OP | State::ERROR_ACK});

    expectAlStatus(newContext, State::PRE_OP);

    expectSyncManagerActivate(0, true);
    expectSyncManagerActivate(1, true);
    expectSyncManagerActivate(2, false);
    expectSyncManagerActivate(3, false);
    preop.onEntry(Context::build(State::SAFE_OP), newContext);
}

TEST_F(ESMStateSafeOPTest, 25_1_ErrSafeOP_to_SafeOP)
{
    expectUpdatePdoInput();
    expectUpdatePdoOutput();

    Context newContext = safeop.routine(Context::build(State::SAFE_OP, StatusCode::INVALID_REQUESTED_STATE_CHANGE),
                                         ALControl{State::SAFE_OP | State::ERROR_ACK});

    expectAlStatus(newContext, State::SAFE_OP);
}

TEST_F(ESMStateSafeOPTest, 29A_SafeOP_to_ErrSafeOP)
{
    expectUpdatePdoInput();
    expectUpdatePdoOutput();

    Context newContext = safeop.routine(Context::build(State::SAFE_OP), ALControl{State::BOOT});

    expectAlStatus(newContext, State::SAFE_OP, StatusCode::INVALID_REQUESTED_STATE_CHANGE);
}

TEST_F(ESMStateSafeOPTest, 29B_ErrSafeOP_to_ErrSafeOP)
{
    expectUpdatePdoInput();
    expectUpdatePdoOutput();


    Context newContext = safeop.routine(Context::build(State::SAFE_OP, StatusCode::UNKNOWN_REQUESTED_STATE),
                                         ALControl{State::BOOT | kickcat::ERROR_ACK});

    expectAlStatus(newContext, State::SAFE_OP, StatusCode::INVALID_REQUESTED_STATE_CHANGE);
}

TEST_F(ESMStateSafeOPTest, 30A_SafeOP_to_ErrSafeOP)
{
    Context newContext = safeop.routine(Context::build(State::SAFE_OP), ALControl{UNKNOWN_STATE});

    expectAlStatus(newContext, State::SAFE_OP, StatusCode::UNKNOWN_REQUESTED_STATE);
}

TEST_F(ESMStateSafeOPTest, 30B_ErrSafeOP_to_ErrSafeOP)
{
    Context newContext = safeop.routine(Context::build(State::SAFE_OP, StatusCode::INVALID_REQUESTED_STATE_CHANGE),
                                         ALControl{static_cast<uint16_t>(UNKNOWN_STATE | State::ERROR_ACK)});

    expectAlStatus(newContext, State::SAFE_OP, StatusCode::UNKNOWN_REQUESTED_STATE);
}

TEST_F(ESMStateSafeOPTest, 31_2_ErrSafeOP_to_ErrSafeOP)
{
    pdo_out.activate = ~SM_ACTIVATE_ENABLE;

    Context newContext = safeop.routine(Context::build(State::SAFE_OP, StatusCode::INVALID_REQUESTED_STATE_CHANGE),
                                         ALControl{State::SAFE_OP});

    expectAlStatus(newContext, State::SAFE_OP, StatusCode::INVALID_REQUESTED_STATE_CHANGE);
}

TEST_F(ESMStateSafeOPTest, 32A_SafeOP_to_PreOP)
{
    pdo_out.activate = ~SM_ACTIVATE_ENABLE;
    expectUpdatePdoInput();
    expectUpdatePdoOutput();

    Context newContext = safeop.routine(Context::build(State::SAFE_OP), ALControl{State::SAFE_OP});

    expectAlStatus(newContext, State::PRE_OP, StatusCode::INVALID_OUTPUT_CONFIGURATION);

    expectSyncManagerActivate(0, true);
    expectSyncManagerActivate(1, true);
    expectSyncManagerActivate(2, false);
    expectSyncManagerActivate(3, false);
    preop.onEntry(Context::build(State::SAFE_OP), newContext);
}

TEST_F(ESMStateSafeOPTest, 32B_SafeOP_to_PreOP)
{
    pdo_in.activate = ~SM_ACTIVATE_ENABLE;
    expectUpdatePdoInput();
    expectUpdatePdoOutput();

    Context newContext = safeop.routine(Context::build(State::SAFE_OP), ALControl{State::SAFE_OP});

    expectAlStatus(newContext, State::PRE_OP, StatusCode::INVALID_INPUT_CONFIGURATION);

    expectSyncManagerActivate(0, true);
    expectSyncManagerActivate(1, true);
    expectSyncManagerActivate(2, false);
    expectSyncManagerActivate(3, false);
    preop.onEntry(Context::build(State::SAFE_OP), newContext);
}

TEST_F(ESMStateSafeOPTest, 33A_SafeOP_to_Init)
{
    mbx_in.activate = ~SM_ACTIVATE_ENABLE;
    expectUpdatePdoInput();
    expectUpdatePdoOutput();

    Context newContext = safeop.routine(Context::build(State::SAFE_OP), ALControl{State::SAFE_OP});

    expectAlStatus(newContext, State::INIT, StatusCode::INVALID_MAILBOX_CONFIGURATION_PREOP);

    expectSyncManagerActivate(0, false);
    expectSyncManagerActivate(1, false);
    expectSyncManagerActivate(2, false);
    expectSyncManagerActivate(3, false);
    init.onEntry(Context::build(State::SAFE_OP), newContext);
}

TEST_F(ESMStateSafeOPTest, 33B_SafeOP_to_Init)
{
    mbx_out.activate = ~SM_ACTIVATE_ENABLE;
    expectUpdatePdoInput();
    expectUpdatePdoOutput();

    Context newContext = safeop.routine(Context::build(State::SAFE_OP), ALControl{State::SAFE_OP});

    expectAlStatus(newContext, State::INIT, StatusCode::INVALID_MAILBOX_CONFIGURATION_PREOP);

    expectSyncManagerActivate(0, false);
    expectSyncManagerActivate(1, false);
    expectSyncManagerActivate(2, false);
    expectSyncManagerActivate(3, false);
    init.onEntry(Context::build(State::SAFE_OP), newContext);
}
