// \brief OS agnostic time API implementation
//
// OS-agnostic pieces only (compiled for every backend, KickOS included): they
// build on now(), which each backend provides. now() and since_unix_epoch()
// live in the per-OS backend TUs selected by lib/CMakeLists.txt.
#include "OS/Time.h"

namespace kickcat
{
    nanoseconds elapsed_time(nanoseconds start)
    {
        return now() - start;
    }

    static nanoseconds start_time = now();
    nanoseconds since_start()
    {
        return now() - start_time;
    }
}
