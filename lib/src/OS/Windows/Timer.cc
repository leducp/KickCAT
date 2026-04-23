#include <cstdio>
#include <thread>

#include "kickcat/Error.h"
#include "kickcat/OS/Timer.h"

namespace kickcat
{
    std::error_code Timer::wait_next_tick()
    {
        nanoseconds now = since_epoch();
        if (next_deadline_ > now)
        {
            std::this_thread::sleep_for(next_deadline_ - now);
        }
        last_wakeup_ = since_epoch();

        next_deadline_ += period_;
        if (next_deadline_ < last_wakeup_)
        {
            printf("!!! LATE !!!\n");
        }
        while (next_deadline_ < last_wakeup_)
        {
            // We are late: compute a new deadline that maintain the cycle.
            next_deadline_ += period_;
        }

        return {};
    }
}
