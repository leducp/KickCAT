#ifndef KICKCAT_OS_TIMER_H
#define KICKCAT_OS_TIMER_H

#include <string_view>
#include <system_error>

#include "kickcat/OS/Time.h"
#include "kickcat/OS/ConditionVariable.h"
#include "kickcat/OS/Mutex.h"

namespace kickcat
{
    class Timer
    {
    public:
        // disable copy
        Timer(Timer const &) = delete;
        void operator=(Timer const &) = delete;

        /// \param  name     Name of the timer (logging)
        /// \param  period   Interval between two timer firing
        Timer(nanoseconds period);
        ~Timer() = default;

        /// Start the timer.
        void start(nanoseconds sync_point = since_epoch());

        ///  Stop the timer.
        void stop();

        ///  Get timer status
        bool is_stopped() const;

        ///  Change the timer values to the new provided values and reset it.
        ///  This does not take effect before the timer is restarted.
        /// \param  period  Interval between two timer firing.
        void update_period(nanoseconds period);

        /// \return timer period/interval
        nanoseconds period() const;

        /// Wait until the next timer tick (blocking call)
        std::error_code wait_next_tick();

        /// \brief  Adjust the timer phase to track an external time reference (e.g. EtherCAT DC).
        /// \details Call this every cycle before wait_next_tick() with bus.dcMasterOffset().
        ///          A positive offset means the master is ahead of the reference: the timer slows down.
        ///          Uses a first-order IIR filter to smooth the correction.
        ///          Pass 0ns when no reference is available (no-op when filtered offset is zero).
        void apply_offset(nanoseconds raw_offset);

    protected:
    private:
        static constexpr int64_t OFFSET_FILTER_DEPTH      = 256;
        static constexpr int64_t OFFSET_CORRECTION_DIVISOR = 16;

        std::string name_;
        nanoseconds period_;
        nanoseconds next_deadline_{};
        nanoseconds last_wakeup_{};
        nanoseconds filtered_offset_{0ns};

        Mutex mutex_{};
        ConditionVariable stop_{};
        bool is_stopped_{true};
    };
}

#endif
