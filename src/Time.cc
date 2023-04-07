// \brief OS agnostic time API implementation
#include "Time.h"

namespace kickcat
{
    extern "C"
    {
        static nanoseconds __since_epoch()
        {
            auto now = time_point_cast<nanoseconds>(system_clock::now());
            return now.time_since_epoch();
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
