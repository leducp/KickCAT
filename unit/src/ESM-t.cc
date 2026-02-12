#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <cstring>
#include "mocks/ESC.h"
#include "kickcat/AbstractESC.h"
#include "kickcat/ESM.h"
#include "kickcat/Error.h"
#include "kickcat/protocol.h"

using namespace kickcat;
using namespace kickcat::ESM;
using namespace testing;

class MockState : public AbstractState
{
public:
    PDO pdo_instance;

    MockState(uint8_t id, AbstractESC& esc)
        : AbstractState(id, esc, pdo_instance), pdo_instance(&esc) {};

    MOCK_METHOD(Context, routine, (Context status, ALControl control), (override));
    MOCK_METHOD(Context, routineInternal, (Context status, ALControl control), (override));
    MOCK_METHOD(void, onEntry, (Context oldStatus, Context newStatus), (override));
};

class StateMachineTest : public testing::Test
{
public:
    MockESC esc_{};
    uint8_t FIRST_STATE_ID{0};
    uint8_t SECOND_STATE_ID{1};
    uint8_t THIRD_STATE_ID{2};
    uint8_t FOURTH_STATE_ID{3};
    MockState firstState{FIRST_STATE_ID, esc_};
    MockState secondState{SECOND_STATE_ID, esc_};
    MockState thirdState{THIRD_STATE_ID, esc_};
    MockState fourthState{FOURTH_STATE_ID, esc_};

    void SetUp() override
    {
        sm = new StateMachine(esc_, {{&firstState, &secondState, &thirdState, &fourthState}});
    }

    void expectAlControlRead(uint16_t alControlValue);
    StateMachine* sm;
};

MATCHER_P2(StatusMatches, expectedAlStatus, expectedALStatusCode, "Matches status")
{
    return arg.al_status == expectedAlStatus and arg.al_status_code == expectedALStatusCode;
}

MATCHER_P(WrittenMatches, expectedValue, "Matches Written Value")
{
    return *(uint16_t const*)arg == expectedValue;
}

MATCHER_P(AlControlMatches, expectedValue, "Matches AL Control")
{
    return arg.value == expectedValue;
}

TEST_F(StateMachineTest, start)
{
    EXPECT_CALL(firstState, onEntry(_, StatusMatches(0, 0)));

    sm->start();
    ASSERT_EQ(sm->state(), FIRST_STATE_ID);
}

TEST_F(StateMachineTest, play_noTransition)
{
    EXPECT_CALL(esc_, read(Eq(reg::AL_CONTROL), _, Eq(sizeof(uint16_t)))).WillOnce(Return(0)).WillOnce(Return(0));
    EXPECT_CALL(esc_, read(Eq(reg::WDOG_STATUS), _, Eq(sizeof(uint16_t)))).WillOnce(Return(0)).WillOnce(Return(0));

    EXPECT_CALL(esc_, write(_, _, _)).Times(0);

    EXPECT_CALL(firstState, routine(_, _)).Times(2);

    EXPECT_CALL(secondState, onEntry(_, _)).Times(0);

    sm->play();
    sm->play();
}

void StateMachineTest::expectAlControlRead(uint16_t alControlValue)
{
    EXPECT_CALL(esc_, read(reg::AL_CONTROL, _, sizeof(uint16_t)))
        .WillOnce(DoAll(
            testing::Invoke([=](uint16_t, void* ptr, uint16_t) { memcpy(ptr, &alControlValue, sizeof(uint16_t)); }),
            Return(0)));
}

TEST_F(StateMachineTest, play_transition)
{
    uint16_t STATUS_CODE      = 0x58;
    uint16_t AL_CONTROL_VALUE = 0x68;

    expectAlControlRead(AL_CONTROL_VALUE);
    EXPECT_CALL(esc_, read(reg::WDOG_STATUS, _, Eq(sizeof(uint16_t)))).WillOnce(Return(0));

    EXPECT_CALL(firstState, routine(_, AlControlMatches(AL_CONTROL_VALUE)))
        .WillOnce(Return(Context::build(SECOND_STATE_ID, STATUS_CODE)));

    EXPECT_CALL(
        secondState,
        onEntry(StatusMatches(FIRST_STATE_ID, 0), StatusMatches(SECOND_STATE_ID | State::ERROR_ACK, STATUS_CODE)))
        .Times(1);

    EXPECT_CALL(esc_, write(reg::AL_STATUS_CODE, WrittenMatches(STATUS_CODE), sizeof(uint16_t))).WillOnce(Return(0));
    EXPECT_CALL(esc_, write(reg::AL_STATUS, WrittenMatches(SECOND_STATE_ID | State::ERROR_ACK), sizeof(uint16_t)))
        .WillOnce(Return(0));

    sm->play();
}

TEST_F(StateMachineTest, play_wrongState_goToDefaultState)
{
    EXPECT_CALL(esc_, read(reg::AL_CONTROL, _, sizeof(uint16_t))).WillRepeatedly(Return(0));
    EXPECT_CALL(esc_, read(reg::WDOG_STATUS, _, Eq(sizeof(uint16_t)))).WillRepeatedly(Return(0));

    EXPECT_CALL(firstState, routine(_, _)).WillOnce(Return(Context::build(SECOND_STATE_ID)));

    EXPECT_CALL(secondState, onEntry(StatusMatches(FIRST_STATE_ID, 0), StatusMatches(SECOND_STATE_ID, 0))).Times(1);

    EXPECT_CALL(esc_, write(reg::AL_STATUS_CODE, WrittenMatches(0), sizeof(uint16_t))).WillOnce(Return(0));
    EXPECT_CALL(esc_, write(reg::AL_STATUS, WrittenMatches(SECOND_STATE_ID), sizeof(uint16_t))).WillOnce(Return(0));

    sm->play();

    EXPECT_CALL(firstState,
                onEntry(StatusMatches(SECOND_STATE_ID, 0), StatusMatches(FIRST_STATE_ID, UNKNOWN_REQUESTED_STATE)))
        .Times(1);

    EXPECT_CALL(esc_, write(reg::AL_STATUS_CODE, WrittenMatches(UNKNOWN_REQUESTED_STATE), sizeof(uint16_t)))
        .WillOnce(Return(0));

    EXPECT_CALL(esc_, write(reg::AL_STATUS, WrittenMatches(FIRST_STATE_ID), sizeof(uint16_t))).WillOnce(Return(0));

    EXPECT_CALL(secondState, routine(_, _)).WillOnce(Return(Context::build(4)));

    sm->play();
}
