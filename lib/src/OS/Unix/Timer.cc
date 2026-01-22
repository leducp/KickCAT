#include <cstdio>
#include <stdexcept>
#include <unistd.h>
#include "kickcat/Error.h"
#include "kickcat/OS/Timer.h"

namespace kickcat
{
    Timer::Timer(nanoseconds period)
        : period_{period}
    {
    }

    nanoseconds Timer::period() const
    {
        return period_;
    }

    void Timer::start(nanoseconds sync_point)
    {
        // Compute the next_deadline according to the sync point (we want to tick such that one of the tick matches the sync point)
        next_deadline_ = sync_point;

        nanoseconds delta = (since_epoch() - next_deadline_);
        if (delta < 0ns)
        {
            THROW_ERROR("Invalid sync point");
        }

        int64_t missing = (delta / period_);
        next_deadline_ += missing * period_ + period_;

        {
            LockGuard lock(mutex_);
            is_stopped_ = false;
        }
        stop_.signal();
    }

    void Timer::stop()
    {
        {
            LockGuard lock(mutex_);
            is_stopped_ = true;
        }
        stop_.signal();
    }

    bool Timer::is_stopped() const
    {
        return is_stopped_;
    }

    void Timer::update_period(nanoseconds period)
    {
        period_ = period;
    }

    std::error_code Timer::wait_next_tick()
    {
        {
            LockGuard lock(mutex_);
            stop_.wait(mutex_, [&]()
                       { return not is_stopped_; });
        }

        // Wait to the next working time.
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
        if (next_deadline_ < last_wakeup_)
        {
            return std::error_code{ETIME, std::system_category()};
        }

        return {};
    }
}
