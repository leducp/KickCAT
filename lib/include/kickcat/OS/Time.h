#ifndef KICKCAT_TIME_H
#define KICKCAT_TIME_H

#include <chrono>
#include <system_error>

namespace kickcat
{
    using namespace std::chrono;
    using seconds_f = std::chrono::duration<float>;

    void sleep(nanoseconds ns);

    // return the time in ns since epoch
    nanoseconds since_epoch();

    // return time in ns since the processus start
    nanoseconds since_start();

    // return the time since since another point in time
    nanoseconds elapsed_time(nanoseconds start = since_epoch());

    // Convert an std::chrono duration to a POSIX timespec
    constexpr timespec to_timespec(nanoseconds time)
    {
        auto secs = duration_cast<seconds>(time);
        nanoseconds nsecs = (time - secs);
        return timespec{static_cast<time_t>(secs.count()), static_cast<long>(nsecs.count())};
    }

    // Convert a POSIX timespec to a std::chrono duration
    constexpr nanoseconds from_timespec(timespec time)
    {
        auto duration = seconds{time.tv_sec} + nanoseconds{time.tv_nsec};
        return duration_cast<nanoseconds>(duration);
    }
}

#endif
