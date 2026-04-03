#ifndef KICKCAT_OS_FUTEX_H
#define KICKCAT_OS_FUTEX_H

#include <atomic>
#include <cstdint>

#include "kickcat/OS/Time.h"

namespace kickcat
{
    /// Block until the low 32 bits of \p word differ from \p expected,
    /// or \p timeout elapses.
    /// Uses inter-process safe primitives (not process-private).
    /// Returns false on timeout, true on wake / spurious / value-change.
    bool futex_wait(std::atomic<uint64_t>& word, uint64_t expected, nanoseconds timeout);

    /// Wake all threads/processes blocked on \p word.
    void futex_wake_all(std::atomic<uint64_t>& word);
}

#endif
