// \brief OS agnostic time API implementation
#include "OS/Time.h"

namespace kickcat
{
    nanoseconds epoch_offset()
    {
        // function-local static: safe even when called during another TU static init
        static nanoseconds const offset = []
        {
            auto now = time_point_cast<nanoseconds>(system_clock::now());
            return now.time_since_epoch() - clock_monotonic();
        }();
        return offset;
    }

    extern "C"
    {
        static nanoseconds __since_epoch()
        {
            return clock_monotonic() + epoch_offset();
        }
    }
    __attribute__((weak,alias("__since_epoch"))) nanoseconds since_epoch();

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
