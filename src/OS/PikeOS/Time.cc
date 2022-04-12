#include "Time.h"
#include "Error.h"

extern "C"
{
    #include <p4.h>
}

namespace kickcat
{
    void sleep(nanoseconds ns)
    {
        p4_sleep(P4_NSEC(ns.count()));
    }
}