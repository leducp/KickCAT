#include <cstdio>
#include <stdexcept>
#include <thread>

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
        nanoseconds now = since_epoch();
        nanoseconds delta = now - sync_point;

        int64_t periods_to_skip = 0;
        if (delta >= 0ns)
        {
            periods_to_skip = (delta / period_) + 1;
        }
        next_deadline_ = sync_point + periods_to_skip * period_;

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

    void Timer::apply_offset(nanoseconds raw_offset)
    {
        filtered_offset_ = (filtered_offset_ * (OFFSET_FILTER_DEPTH - 1) + raw_offset) / OFFSET_FILTER_DEPTH;
        next_deadline_ -= filtered_offset_ / OFFSET_CORRECTION_DIVISOR;
    }

    std::error_code Timer::wait_next_tick()
    {
        {
            LockGuard lock(mutex_);
            stop_.wait(mutex_, [&]()
                       { return not is_stopped_; });
        }

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

        return {};
    }
}
