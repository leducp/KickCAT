#include "kickmsg/OS/Futex.h"

#include <cerrno>
#include <climits>

#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>

// Futex operates on the low 32 bits of a uint64_t. This requires
// little-endian so that reinterpret_cast<uint32_t*> points to the
// low half. x86-64 and AArch64 (default) are both little-endian.
static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__,
    "Futex implementation requires little-endian byte order");

namespace kickmsg
{

    bool futex_wait(std::atomic<uint64_t>& word, uint64_t expected,
                    std::chrono::nanoseconds timeout)
    {
        auto* addr = reinterpret_cast<uint32_t*>(&word);
        auto  val  = static_cast<uint32_t>(expected);

        struct timespec ts;
        ts.tv_sec  = static_cast<time_t>(timeout.count() / 1'000'000'000);
        ts.tv_nsec = static_cast<long>(timeout.count() % 1'000'000'000);

        long rc = syscall(SYS_futex, addr, FUTEX_WAIT, val, &ts, nullptr, 0);
        return !(rc == -1 && errno == ETIMEDOUT);
    }

    void futex_wake_all(std::atomic<uint64_t>& word)
    {
        auto* addr = reinterpret_cast<uint32_t*>(&word);
        syscall(SYS_futex, addr, FUTEX_WAKE, INT_MAX, nullptr, nullptr, 0);
    }

} // namespace kickmsg
