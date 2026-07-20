#include <cstdio>
#include <thread>

#include "kickcat/Error.h"
#include "kickcat/OS/Timer.h"

namespace kickcat
{
    std::error_code Timer::wait_next_tick()
    {
        nanoseconds current = now();
        if (next_deadline_ > current)
        {
            std::this_thread::sleep_for(next_deadline_ - current);
        }
        last_wakeup_ = now();

        next_deadline_ += period_;
        last_overran_ = false;
        if (next_deadline_ < last_wakeup_)
        {
            printf("!!! LATE !!!\n");
            last_overran_ = true;
        }
        while (next_deadline_ < last_wakeup_)
        {
            // We are late: compute a new deadline that maintain the cycle.
            next_deadline_ += period_;
        }

        return {};
    }
}
