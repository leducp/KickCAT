#ifndef KICKCAT_TIME_H
#define KICKCAT_TIME_H

#include <chrono>
#include <system_error>

namespace kickcat
{
    using namespace std::chrono;

    void sleep(nanoseconds ns);

    // return the time in ns since epoch
    nanoseconds since_epoch() __attribute__((weak));

    nanoseconds elapsed_time(nanoseconds start = since_epoch());
}

#endif
