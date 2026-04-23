// \brief OS agnostic Timer API - shared logic
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

    void Timer::init(nanoseconds sync_point)
    {
        nanoseconds now = since_epoch();
        nanoseconds delta = now - sync_point;

        int64_t periods_to_skip = 0;
        if (delta >= 0ns)
        {
            periods_to_skip = (delta / period_) + 1;
        }
        next_deadline_ = sync_point + periods_to_skip * period_;

        last_raw_offset_ = 0ns;
        filtered_drift_  = 0ns;
    }

    void Timer::update_period(nanoseconds period)
    {
        period_ = period;
    }

    void Timer::apply_offset(nanoseconds raw_offset)
    {
        if (last_raw_offset_ != 0ns)
        {
            nanoseconds delta = raw_offset - last_raw_offset_;
            filtered_drift_ = (filtered_drift_ * (DRIFT_FILTER_DEPTH - 1) + delta) / DRIFT_FILTER_DEPTH;
            next_deadline_ += filtered_drift_;
        }
        last_raw_offset_ = raw_offset;
    }
}
