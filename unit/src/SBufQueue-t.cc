#include <gtest/gtest.h>

#include "kickcat/SBufQueue.h"
#include "kickcat/TapSocket.h" // reuse the SBufQueue instance for the test

using namespace kickcat;

class TestSBufQueue :  public ::testing::Test
{
public:
    void SetUp() override
    {
        producer_.initContext();

        ASSERT_EQ(TapSocket::QUEUE::depth(), producer_.freed());
        ASSERT_EQ(TapSocket::QUEUE::depth(), consummer_.freed());

        ASSERT_EQ(0, producer_.readied());
        ASSERT_EQ(0, consummer_.readied());
    }

    TapSocket::QUEUE::Context producerCtx_;
    TapSocket::QUEUE producer_ {producerCtx_};

    TapSocket::QUEUE::Context consumerCtx_;
    TapSocket::QUEUE consummer_{consumerCtx_};
};
