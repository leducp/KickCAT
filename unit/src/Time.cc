
#include "kickcat/OS/Time.h"

namespace kickcat
{
    static nanoseconds mock_now;

    void resetSinceEpoch()
    {
        auto now = time_point_cast<nanoseconds>(system_clock::now());
        mock_now = now.time_since_epoch();
    }

    nanoseconds since_epoch()
    {
        mock_now += 1ms;
        return mock_now;
    }
}
