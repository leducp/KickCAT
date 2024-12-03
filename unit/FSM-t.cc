#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <cstring>
#include "kickcat/AbstractESC.h"
#include "kickcat/FSM.h"
#include "kickcat/OS/Time.h"
#include "kickcat/Error.h"

using namespace kickcat;
using namespace kickcat::FSM;
using namespace testing;


class MockESC2 : public kickcat::AbstractESC
{
public:
    virtual int32_t read(uint16_t address, void* data, uint16_t size)
    {
        return 0;
    }

    virtual int32_t write(uint16_t address, void const* data, uint16_t size)
    {
        return 0;
    }

    hresult initInternal()
    {
        return hresult::OK;
    }
};

class MockState : public AbstractState
{
public:
    MockESC2 esc_;

    MockState(uint8_t id)
        : AbstractState(id, esc_) {};
    MOCK_METHOD(void, routine, (), (override));
    MOCK_METHOD(uint8_t, transition, (), (override));
    MOCK_METHOD(void, onEntry, (uint8_t), (override));
    MOCK_METHOD(void, onExit, (uint8_t), (override));
};

class StateMachineTest : public testing::Test
{
public:
    MockState firstState{0};
    MockState secondState{1};

    StateMachine sm{{&firstState, &secondState}};
};

TEST_F(StateMachineTest, start)
{
    EXPECT_CALL(firstState, onEntry(firstState.id()));

    sm.start();
}

TEST_F(StateMachineTest, play_noTransition)
{
    EXPECT_CALL(firstState, routine()).Times(2);

    EXPECT_CALL(firstState, transition()).Times(2).WillRepeatedly(Return(firstState.id()));

    EXPECT_CALL(firstState, onExit(_)).Times(0);
    EXPECT_CALL(secondState, onEntry(_)).Times(0);

    sm.play();
    sm.play();
}

TEST_F(StateMachineTest, play_transition)
{
    EXPECT_CALL(firstState, routine());

    EXPECT_CALL(firstState, transition()).WillOnce(Return(secondState.id()));

    EXPECT_CALL(firstState, onExit(secondState.id())).Times(1);
    EXPECT_CALL(secondState, onEntry(firstState.id())).Times(1);

    sm.play();
}

TEST_F(StateMachineTest, play_wrongState_stayInDefaultState)
{
    EXPECT_CALL(firstState, routine());

    EXPECT_CALL(firstState, transition()).WillOnce(Return(3));

    EXPECT_CALL(firstState, onExit(_)).Times(0);
    EXPECT_CALL(secondState, onEntry(_)).Times(0);

    sm.play();
}

TEST_F(StateMachineTest, play_wrongState_goToDefaultState)
{
    EXPECT_CALL(firstState, routine());

    EXPECT_CALL(firstState, transition()).WillOnce(Return(secondState.id()));

    EXPECT_CALL(firstState, onExit(secondState.id())).Times(1);
    EXPECT_CALL(secondState, onEntry(firstState.id())).Times(1);

    sm.play();

    EXPECT_CALL(secondState, routine());
    EXPECT_CALL(secondState, transition()).WillOnce(Return(3));

    EXPECT_CALL(secondState, onExit(firstState.id())).Times(1);
    EXPECT_CALL(firstState, onEntry(secondState.id())).Times(1);

    sm.play();
}
