#ifndef KICKCAT_OS_LINUX_MUTEX_H
#define KICKCAT_OS_LINUX_MUTEX_H

#include "os_types.h"

namespace kickcat
{
    class ConditionVariable;

    class Mutex
    {
        friend ConditionVariable;
    public:
        /// \param mutex    nullptr for a private mutex, a pointer to a shared segment otherwise.
        Mutex(os_mutex& mutex) : Mutex(&mutex) {}
        Mutex(os_mutex* mutex = nullptr);
        ~Mutex();

        /// \brief  Initialise the mutex. Automatically called by the constructor
        ///         if the mutex is private.
        void init();

        /// \brief  Lock the mutex
        void lock();

        /// \brief  Unlock the mutex
        void unlock();

        /// \brief  Try to lock the mutex (non-blocking)
        bool tryLock();

    private:
        /// OS opaque type protecting the value.
        os_mutex* pmutex_;
        os_mutex mutex_;
    };


    /// RAII lock manager
    class LockGuard
    {
    public:
        /// \param       lock    mutex to be acquired by the locker
        LockGuard(Mutex& mutex);
        ~LockGuard();

    private:
        Mutex& mutex_;  ///< Reference to the mutex managed by the guard
    };
}

#endif
