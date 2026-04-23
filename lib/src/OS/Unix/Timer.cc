#include <stdexcept>
#include <unistd.h>

#include "kickcat/debug.h"
#include "kickcat/Error.h"
#include "kickcat/OS/Timer.h"

namespace kickcat
{
    std::error_code Timer::wait_next_tick()
    {
        timespec const deadline = to_timespec(next_deadline_);
        int rc = clock_nanosleep(CLOCK_REALTIME, TIMER_ABSTIME, &deadline, NULL);
        if (rc != 0)
        {
            if (rc == EINTR)
            {
                // timer interrupted: to be called again if needed by the client
                return std::error_code{rc, std::system_category()};
            }

            // no recoverable errors (wrong clock, bad deadline, bad deadline **address**)
            throw std::system_error(rc, std::system_category(), "clock_nanosleep()");
        }
        last_wakeup_ = since_epoch();

        // Calculate next deadline.
        next_deadline_ += period_;

        std::error_code ret{};
        if (next_deadline_ < last_wakeup_)
        {
            dc_error("!!! LATE !!!\n");
            ret = std::error_code{ETIME, std::system_category()};
        }
        while (next_deadline_ < last_wakeup_)
        {
            // We are late: compute a new deadline that maintain the cycle.
            next_deadline_ += period_;
        }

        return ret;
    }
}
