#include "OS/Time.h"
#include "Error.h"

extern "C"
{
    #include <p4.h>
}

namespace kickcat
{
    nanoseconds clock_monotonic()
    {
        // p4_get_time(): nanoseconds since boot, monotonic
        return nanoseconds(p4_get_time());
    }

    void sleep(nanoseconds ns)
    {
        p4_sleep(P4_NSEC(ns.count()));
    }
}
