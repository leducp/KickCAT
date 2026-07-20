#include "OS/Time.h"
#include "Error.h"

namespace kickcat
{
    // clock_gettime(), not std::chrono::steady_clock: on some toolchains
    // (e.g. arm-none-eabi) libstdc++ maps steady_clock to the same wall-clock
    // syscall as system_clock, silently losing monotonicity.
    // Also serves NuttX, which requires CONFIG_CLOCK_MONOTONIC=y (off by default).
    nanoseconds now()
    {
        timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return from_timespec(ts);
    }

    nanoseconds since_unix_epoch()
    {
        timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        return from_timespec(ts);
    }

    void sleep(nanoseconds ns)
    {
        // convert chrono to OS timespec
        auto secs = duration_cast<seconds>(ns);
        nanoseconds nsecs = (ns - secs);
        timespec remaining_time{secs.count(), nsecs.count()};

        while (true)
        {
            timespec required_time = remaining_time;

            // remaining time
            int32_t result = clock_nanosleep(CLOCK_MONOTONIC, 0, &required_time, &remaining_time);
            if (result == 0)
            {
                return;
            }

            if (result == EINTR)
            {
                // call interrupted by a POSIX signal: must sleep again.
                continue;
            }

            // only possible if timespec is wrongly defined or wrong clock ID
            THROW_SYSTEM_ERROR("clock_nanosleep()");
        }
    }
}
