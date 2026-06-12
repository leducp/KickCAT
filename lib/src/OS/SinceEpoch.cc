// \brief OS agnostic time API - default implementation for since_epoch()
#include "OS/Time.h"

namespace kickcat
{
    nanoseconds since_epoch()
    {
        return clock_monotonic() + epoch_offset();
    }
}
