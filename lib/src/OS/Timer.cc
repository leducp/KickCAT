// \brief OS agnostic Timer API - shared logic
#include "kickcat/OS/Timer.h"

namespace kickcat
{
    Timer::Timer(nanoseconds period, SoftPll::Config const& pll_config)
        : period_{period}
        , pll_config_{pll_config}
        , pll_{period, pll_config}
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
    }

    void Timer::update_period(nanoseconds period)
    {
        period_ = period;
        // The grid changed, so the PLL's learned target phase is stale: rebuild on the new cycle.
        pll_ = SoftPll{period, pll_config_};
    }
}
