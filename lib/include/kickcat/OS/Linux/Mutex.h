#ifndef KICKCAT_OS_LINUX_MUTEX_H
#define KICKCAT_OS_LINUX_MUTEX_H

#include <pthread.h>

namespace kickcat
{
    class ConditionVariable;

    class Mutex
    {
        friend ConditionVariable;
    public:
        /// \param mutex    nullptr for a private mutex, a pointer to a shared segment otherwise.
        Mutex(pthread_mutex_t& mutex) : Mutex(&mutex) {}
        Mutex(pthread_mutex_t* mutex = nullptr);
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
        pthread_mutex_t* pmutex_;
        pthread_mutex_t mutex_;
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
