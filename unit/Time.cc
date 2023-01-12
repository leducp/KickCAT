
#include "kickcat/Time.h"

namespace kickcat
{
    nanoseconds since_epoch()
    {
        auto now = time_point_cast<nanoseconds>(system_clock::now());
        static nanoseconds mock_now = now.time_since_epoch();

        mock_now += 1ms;
        return mock_now;
    }
}
