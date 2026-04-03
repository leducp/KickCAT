#include "kickcat/OS/Futex.h"

#include <cerrno>
#include <climits>

#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace kickcat
{

    bool futex_wait(std::atomic<uint64_t>& word, uint64_t expected, nanoseconds timeout)
    {
        auto* addr = reinterpret_cast<uint32_t*>(&word);
        auto  val  = static_cast<uint32_t>(expected);

        struct timespec ts = to_timespec(timeout);

        long rc = syscall(SYS_futex, addr, FUTEX_WAIT, val, &ts, nullptr, 0);
        return not (rc == -1 and errno == ETIMEDOUT);
    }

    void futex_wake_all(std::atomic<uint64_t>& word)
    {
        auto* addr = reinterpret_cast<uint32_t*>(&word);
        syscall(SYS_futex, addr, FUTEX_WAKE, INT_MAX, nullptr, nullptr, 0);
    }

}
