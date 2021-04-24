#include <ctime>
#include <cstdio>
#include <cerrno>

#include "Time.h"

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
            perror("something wrong happened");
            break;
        }
    }


    nanoseconds since_epoch()
    {
        auto now = time_point_cast<nanoseconds>(system_clock::now());
        return now.time_since_epoch();
    }


    nanoseconds elapsed_time(nanoseconds start)
    {
        return since_epoch() - start;
    }
}