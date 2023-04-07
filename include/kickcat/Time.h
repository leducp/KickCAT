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

    nanoseconds elapsed_time(nanoseconds start = since_epoch());
}

#endif
