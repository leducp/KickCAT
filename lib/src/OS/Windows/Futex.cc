#include "kickcat/OS/Futex.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace kickcat
{
    bool futex_wait(std::atomic<uint64_t>& word, uint64_t expected, nanoseconds timeout)
    {
        auto* addr = reinterpret_cast<void*>(&word);
        auto  val  = static_cast<uint32_t>(expected);

        auto ms = duration_cast<milliseconds>(timeout);
        DWORD timeout_ms = static_cast<DWORD>(ms.count());

        BOOL ok = WaitOnAddress(addr, &val, sizeof(val), timeout_ms);
        return ok or (GetLastError() != ERROR_TIMEOUT);
    }

    void futex_wake_all(std::atomic<uint64_t>& word)
    {
        WakeByAddressAll(reinterpret_cast<void*>(&word));
    }
}
