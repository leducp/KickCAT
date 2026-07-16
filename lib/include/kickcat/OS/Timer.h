#ifndef KICKCAT_OS_TIMER_H
#define KICKCAT_OS_TIMER_H

#include <system_error>

#include "kickcat/OS/SoftPll.h"
#include "kickcat/OS/Time.h"

namespace kickcat
{
    class Timer
    {
    public:
        // disable copy
        Timer(Timer const &) = delete;
        void operator=(Timer const &) = delete;

        /// \param  period      Interval between two timer firing
        /// \param  pll_config  gains/limits for the soft PLL disciplining the cadence (see sync_to)
        Timer(nanoseconds period, SoftPll::Config const& pll_config = {});
        ~Timer() = default;

        /// \brief  Start (or restart) the timer.
        /// \details Aligns the next deadline to sync_point + N*period so that it lies in the future.
        ///          Safe to call multiple times.
        void start(nanoseconds sync_point = since_epoch());

        ///  Change the timer values to the new provided values and reset it.
        ///  This does not take effect before the timer is restarted.
        /// \param  period  Interval between two timer firing.
        void update_period(nanoseconds period);

        /// \return timer period/interval
        nanoseconds period() const;

        /// \brief Wait until the next timer tick (blocking call).
        /// \return an error_code carrying ETIME if the cycle woke late (see overran()).
        std::error_code wait_next_tick();

        /// \return true if the last wait_next_tick() woke late (deadline already past)
        bool overran() const { return last_overran_; }

        /// \brief Feed one DC reference sample to the soft PLL and nudge the loop deadline by the
        ///        resulting phase correction (phase only; the period is unchanged). Call once per
        ///        cycle before wait_next_tick; prefer Bus::sync, which supplies the reference.
        /// \param reference_system_time  reference clock, raw ns
        void sync_to(uint64_t reference_system_time)
        {
            next_deadline_ += pll_.update(reference_system_time, last_overran_);
        }

        /// \brief Coast one cycle with no fresh reference: hold the PLL's bias, don't learn. Prefer
        ///        Bus::sync, which chooses sync_to vs coast for you.
        void coast()
        {
            next_deadline_ += pll_.coast();
        }

        /// \return true once the soft PLL has held phase lock (readiness gate for the application)
        bool locked() const { return pll_.locked(); }

        /// \return the soft PLL, for diagnostics (phase error, filtered error, sample count)
        SoftPll const& pll() const { return pll_; }

    private:
        nanoseconds     period_;
        nanoseconds     next_deadline_{};
        nanoseconds     last_wakeup_{};
        bool            last_overran_{false};
        SoftPll::Config pll_config_;
        SoftPll         pll_;
    };
}

#endif
