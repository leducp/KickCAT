#include "kickcat/OS/Time.h"

// Deterministic test clock: the only Time.cc in the unit-test build, so it also provides
// sleep(). now() (monotonic) and since_unix_epoch() (wall) advance SEPARATE counters, each
// 1 ms per call. now() must stay in the monotonic domain (small, from 0): Timer and
// ConditionVariable feed now()-derived deadlines to CLOCK_MONOTONIC OS primitives, so a
// wall-seeded now() would put every deadline ~decades in the future and hang.
namespace kickcat
{
    static nanoseconds mock_mono;
    static nanoseconds mock_wall;

    void resetMockClock()
    {
        // 1s, not 0: must dominate DS402's zero-initialised timestamp compares (periods <= 100ms)
        // yet stay far below real monotonic uptime so Timer's absolute CLOCK_MONOTONIC sleep
        // sees a past deadline and returns at once instead of waiting.
        mock_mono = seconds{1};
        auto start = time_point_cast<nanoseconds>(system_clock::now());
        mock_wall = start.time_since_epoch();
    }

    nanoseconds now()
    {
        mock_mono += 1ms;
        return mock_mono;
    }

    nanoseconds since_unix_epoch()
    {
        mock_wall += 1ms;
        return mock_wall;
    }

    void sleep(nanoseconds ns)
    {
        (void)ns;
    }
}
