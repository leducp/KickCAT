#include "kickcat/Time.h"
#include "Arduino.h"

namespace kickcat
{
    nanoseconds since_epoch()
    {
        auto now = time_point_cast<nanoseconds>(system_clock::now());
        return now.time_since_epoch();
    }

    nanoseconds elapsed_time(nanoseconds start)
    {
        return since_epoch() - start;
    }

    static nanoseconds start_time = since_epoch();
    nanoseconds since_start()
    {
        return since_epoch() - start_time;
    }
}
