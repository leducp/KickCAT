#include "OS/Time.h"
#include "Error.h"

namespace kickcat
{
    // MSVC/mingw's steady_clock is QueryPerformanceCounter-backed and
    // conformant here (unlike arm-none-eabi's libstdc++), so chrono is fine.
    nanoseconds now()
    {
        return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch());
    }

    nanoseconds since_unix_epoch()
    {
        return duration_cast<nanoseconds>(system_clock::now().time_since_epoch());
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
