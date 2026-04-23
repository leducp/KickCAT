#ifndef KICKCAT_OS_TIMER_H
#define KICKCAT_OS_TIMER_H

#include <system_error>

#include "kickcat/OS/Time.h"

namespace kickcat
{
    class Timer
    {
    public:
        // disable copy
        Timer(Timer const &) = delete;
        void operator=(Timer const &) = delete;

        /// \param  period   Interval between two timer firing
        Timer(nanoseconds period);
        ~Timer() = default;

        /// \brief  Initialize (or restart) the timer.
        /// \details Aligns the next deadline to sync_point + N*period so that it lies in the future.
        ///          Resets the drift compensation state. Safe to call multiple times.
        void init(nanoseconds sync_point = since_epoch());

        /// \deprecated Use init() instead.
        void start(nanoseconds sync_point = since_epoch()) { init(sync_point); }

        ///  Change the timer values to the new provided values and reset it.
        ///  This does not take effect before the timer is restarted.
        /// \param  period  Interval between two timer firing.
        void update_period(nanoseconds period);

        /// \return timer period/interval
        nanoseconds period() const;

        /// Wait until the next timer tick (blocking call)
        std::error_code wait_next_tick();

        /// \brief  Phase-lock the timer to an external time reference (e.g. EtherCAT DC).
        /// \details Pass the master-vs-reference offset every cycle before wait_next_tick().
        ///          Do not call when no drift compensation is desired.
        void apply_offset(nanoseconds raw_offset);

        /// \return filtered per-cycle drift estimate applied by apply_offset(). 0ns if apply_offset()
        ///         has never been called; typically a few tens of ns at steady state.
        nanoseconds filtered_drift() const { return filtered_drift_; }

    private:
        static constexpr int64_t DRIFT_FILTER_DEPTH = 256;

        nanoseconds period_;
        nanoseconds next_deadline_{};
        nanoseconds last_wakeup_{};
        nanoseconds last_raw_offset_{0ns};
        nanoseconds filtered_drift_{0ns};
    };
}

#endif
