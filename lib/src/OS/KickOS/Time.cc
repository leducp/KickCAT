// KickOS backend for the OS-agnostic time API.
//
// GOTCHA: this toolchain's libstdc++ compiles steady_clock::now() and
// system_clock::now() to the SAME gettimeofday call, so now() must NOT route through
// steady_clock or it would jump on a wall-clock resync. now()/sleep() therefore call
// kos_clock_now()/kos_sleep_ns() directly (newlib also has no clock_nanosleep here).
// since_unix_epoch() uses system_clock, backed by KickOS's settable _gettimeofday
// (kos_clock_set_realtime; 0 offset => boot-relative until synced).
#include <chrono>
#include <cstdint>

#include "OS/Time.h"

extern "C"
{
    uint64_t kos_clock_now(void);
    void     kos_sleep_ns(uint64_t ns);
}

namespace kickcat
{
    void sleep(nanoseconds ns)
    {
        int64_t count = ns.count();
        if (count < 0)
        {
            count = 0;
        }
        kos_sleep_ns(static_cast<uint64_t>(count));
    }

    nanoseconds now()
    {
        return nanoseconds{static_cast<int64_t>(kos_clock_now())};
    }

    nanoseconds since_unix_epoch()
    {
        return duration_cast<nanoseconds>(std::chrono::system_clock::now().time_since_epoch());
    }
}
