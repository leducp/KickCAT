#include <chrono>

#include "OS/Time.h"

extern "C"
{
    #include <p4.h>
}

namespace kickcat
{
    nanoseconds now()
    {
        return duration_cast<nanoseconds>(std::chrono::steady_clock::now().time_since_epoch());
    }

    nanoseconds since_unix_epoch()
    {
        return duration_cast<nanoseconds>(std::chrono::system_clock::now().time_since_epoch());
    }

    void sleep(nanoseconds ns)
    {
        p4_sleep(P4_NSEC(ns.count()));
    }
}
