#ifndef KICKCAT_OS_CONDITION_VARIABLE_H
#define KICKCAT_OS_CONDITION_VARIABLE_H

#include <functional>

#include "kickcat/OS/Time.h"
#include "kickcat/OS/Mutex.h"

namespace kickcat
{
    class ConditionVariable
    {
    public:
        /// \param cond    nullptr for a private cond, a pointer to a shared segment otherwise.
        ConditionVariable(os_cond& cond) : ConditionVariable(&cond) {};
        ConditionVariable(os_cond* cond = nullptr);
        ~ConditionVariable();

        /// \brief Initialise the condition variable. Automatically called by the constructor
        ///        if the condition variable is private.
        void init();

        /// Wait for a signal
        /// \warning Mutex is assumed to be already locked
        void wait(Mutex& mutex, std::function<bool(void)> stopWaiting);

        /// Wait for a signal
        /// \warning Mutex is assumed to be already locked
        int wait_until(Mutex& mutex, nanoseconds timeout, std::function<bool(void)> stopWaiting);

        /// Send a signal to waiting threads
        /// \warning the corresponding lock is assumed to be already locked
        void signal();

    private:
        os_cond* pcond_;
        os_cond cond_;
    };
}

#endif
