#ifndef SLAVE_KICKCAT_TIME_H
#define SLAVE_KICKCAT_TIME_H

#include <chrono>

namespace kickcat
{
    using namespace std::chrono;

    void sleep(nanoseconds ns);

    // return the time in ns since epoch
    nanoseconds since_epoch();

    // return time in ns since the processus start
    nanoseconds since_start();

    nanoseconds elapsed_time(nanoseconds start = since_epoch());
}

#endif
