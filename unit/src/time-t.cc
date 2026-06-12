#include <gtest/gtest.h>

#include "kickcat/OS/Time.h"
#include "mocks/Time.h"

using namespace kickcat;

TEST(Time, clock_monotonic_is_monotonic)
{
    nanoseconds previous = clock_monotonic();
    for (int i = 0; i < 100; ++i)
    {
        nanoseconds current = clock_monotonic();
        ASSERT_GE(current, previous);
        previous = current;
    }

    sleep(1ms);
    ASSERT_GT(clock_monotonic(), previous);
}

TEST(Time, epoch_offset_is_stable)
{
    nanoseconds offset = epoch_offset();
    sleep(1ms);
    ASSERT_EQ(offset, epoch_offset());
}

// since_epoch() is overridden by the mock in the unit binary: each call advances by exactly 1ms
TEST(Time, since_epoch_mock_seam)
{
    resetSinceEpoch();
    nanoseconds t0 = since_epoch();
    ASSERT_EQ(1ms, since_epoch() - t0);
    ASSERT_EQ(2ms, since_epoch() - t0);
}
