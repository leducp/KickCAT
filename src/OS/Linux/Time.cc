#include "Time.h"
#include "Error.h"

namespace kickcat
{
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
            int32_t result = clock_nanosleep(CLOCK_REALTIME, 0, &required_time, &remaining_time);
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