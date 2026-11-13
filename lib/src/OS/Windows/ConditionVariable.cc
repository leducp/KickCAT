#include <cerrno>

#include "Error.h"
#include "OS/ConditionVariable.h"

namespace kickcat
{
    // MSVC backend on the native condition variable, used with the SRW lock of Mutex. Like it, it
    // is not shareable between processes.
    ConditionVariable::ConditionVariable(os_cond* cond)
        : pcond_(cond)
        , cond_()
    {
        if (cond == nullptr)
        {
            pcond_ = &cond_;
            init();
        }
    }


    // A native condition variable owns no resource: nothing to release.
    ConditionVariable::~ConditionVariable() = default;


    void ConditionVariable::init()
    {
        InitializeConditionVariable(pcond_);
    }


    void ConditionVariable::wait(Mutex& mutex, std::function<bool(void)> stopWaiting)
    {
        while (not stopWaiting())
        {
            if (SleepConditionVariableSRW(pcond_, mutex.pmutex_, INFINITE, 0) == 0)
            {
                throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), LOCATION(": SleepConditionVariableSRW()"));
            }
        }
    }


    int ConditionVariable::wait_until(Mutex& mutex, nanoseconds timeout, std::function<bool(void)> stopWaiting)
    {
        nanoseconds deadline = now() + timeout;

        while (not stopWaiting())
        {
            nanoseconds remaining = deadline - now();
            if (remaining <= 0ns)
            {
                return ETIMEDOUT;
            }

            // The native wait takes a relative timeout in ms: round up to never wake before the
            // deadline, and stay below INFINITE.
            milliseconds wait_ms = duration_cast<milliseconds>(remaining + 1ms - 1ns);
            if (wait_ms.count() >= INFINITE)
            {
                wait_ms = milliseconds{INFINITE - 1};
            }

            if (SleepConditionVariableSRW(pcond_, mutex.pmutex_, static_cast<DWORD>(wait_ms.count()), 0) == 0)
            {
                if (GetLastError() == ERROR_TIMEOUT)
                {
                    return ETIMEDOUT;
                }
                throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), LOCATION(": SleepConditionVariableSRW()"));
            }
        }

        return 0;
    }


    void ConditionVariable::signal()
    {
        WakeConditionVariable(pcond_);
    }
}
