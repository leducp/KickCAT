#include "kickmsg/OS/Futex.h"

#include <cerrno>
#include <cstdint>

// macOS kernel ulock API.
// Used by libc++ (std::atomic::wait) and libdispatch since macOS 10.12.
// WARNING: __ulock_wait / __ulock_wake are private Apple APIs with no public
// header. The ABI has been stable for years and is unlikely to change without
// a libc++ rebuild, but Apple has not formally committed to keeping it.
// If Apple removes or changes this interface in a future macOS release,
// a fallback to kqueue EVFILT_USER or dispatch_semaphore may be needed.
extern "C"
{
    int __ulock_wait(uint32_t operation, void* addr, uint64_t value,
                     uint32_t timeout_us);
    int __ulock_wake(uint32_t operation, void* addr, uint64_t wake_value);
}

// UL_COMPARE_AND_WAIT_SHARED: 32-bit compare-and-wait on shared memory
// ULF_NO_ERRNO: return -errno directly instead of setting errno
// ULF_WAKE_ALL: wake all waiters
constexpr uint32_t UL_COMPARE_AND_WAIT_SHARED = 0x00000003;
constexpr uint32_t ULF_NO_ERRNO               = 0x01000000;
constexpr uint32_t ULF_WAKE_ALL               = 0x00000100;

namespace kickmsg
{

    bool futex_wait(std::atomic<uint64_t>& word, uint64_t expected,
                    std::chrono::nanoseconds timeout)
    {
        auto* addr = reinterpret_cast<uint32_t*>(&word);
        auto  val  = static_cast<uint64_t>(static_cast<uint32_t>(expected));

        auto us = std::chrono::duration_cast<std::chrono::microseconds>(timeout).count();
        auto timeout_us = static_cast<uint32_t>(us > 0 ? us : 1);

        int rc = __ulock_wait(
            UL_COMPARE_AND_WAIT_SHARED | ULF_NO_ERRNO,
            addr, val, timeout_us);

        return rc != -ETIMEDOUT;
    }

    void futex_wake_all(std::atomic<uint64_t>& word)
    {
        auto* addr = reinterpret_cast<uint32_t*>(&word);
        __ulock_wake(
            UL_COMPARE_AND_WAIT_SHARED | ULF_WAKE_ALL,
            addr, 0);
    }

} // namespace kickmsg
