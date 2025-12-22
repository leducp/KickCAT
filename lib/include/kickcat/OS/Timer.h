#ifndef GS_CORE_OS_TIMER_H
#define GS_CORE_OS_TIMER_H

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
        void start();

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

    protected:
    private:
        std::string name_;
        nanoseconds period_;
        nanoseconds next_deadline_{};
        nanoseconds last_wakeup_{};

        Mutex mutex_{};
        ConditionVariable stop_{};
        bool is_stopped_{true};
    };
}

#endif
