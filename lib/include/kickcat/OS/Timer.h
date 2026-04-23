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

        /// Wait until the next timer tick (blocking call)
        std::error_code wait_next_tick();

    private:
        nanoseconds period_;
        nanoseconds next_deadline_{};
        nanoseconds last_wakeup_{};
    };
}

#endif
