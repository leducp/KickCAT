// \brief OS agnostic time API - default implementation for since_epoch()
#include "OS/Time.h"

namespace kickcat
{
    nanoseconds since_epoch()
    {
        auto now = time_point_cast<nanoseconds>(system_clock::now());
        return now.time_since_epoch();
    }
}
