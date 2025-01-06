#include "Error.h"
#include "OS/Mutex.h"

namespace kickcat
{
    Mutex::Mutex(pthread_mutex_t* mutex)
        : pmutex_(mutex)
        , mutex_()
    {
        if (mutex == nullptr)
        {
            pmutex_ = &mutex_;
            init();
        }
    }


    Mutex::~Mutex()
    {
        if (pmutex_ == &mutex_)
        {
            int rc = pthread_mutex_destroy(pmutex_);
            if (rc != 0)
            {
                THROW_SYSTEM_ERROR_CODE("pthread_mutex_destroy()", rc);
            }
        }
    }


    void Mutex::init()
    {
        pthread_mutexattr_t attr;
        int rc = pthread_mutexattr_init(&attr);
        if (rc != 0)
        {
            THROW_SYSTEM_ERROR_CODE("pthread_mutexattr_init()", rc);
        }

#ifndef __MINGW64__
        rc = pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT);
        if (rc != 0)
        {
            THROW_SYSTEM_ERROR_CODE("pthread_mutexattr_setprotocol()", rc);
        }

        if (&mutex_ != pmutex_)
        {
            rc = pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
            if (rc != 0)
            {
                THROW_SYSTEM_ERROR_CODE("pthread_mutexattr_setpshared()", rc);
            }
        }
#endif

        rc = pthread_mutex_init(pmutex_, &attr);
        if (rc != 0)
        {
            THROW_SYSTEM_ERROR_CODE("pthread_mutex_init()", rc);
        }

        rc = pthread_mutexattr_destroy(&attr);
        if (rc != 0)
        {
            THROW_SYSTEM_ERROR_CODE("pthread_mutexattr_destroy()", rc);
        }
    }


    void Mutex::lock()
    {
        int rc = pthread_mutex_lock(pmutex_);
        if (rc != 0)
        {
            THROW_SYSTEM_ERROR_CODE("pthread_mutex_lock()", rc);
        }
    }


    void Mutex::unlock()
    {
        int rc = pthread_mutex_unlock(pmutex_);
        if (rc != 0)
        {
            THROW_SYSTEM_ERROR_CODE("pthread_mutex_unlock()", rc);
        }
    }


    bool Mutex::tryLock()
    {
        int rc = pthread_mutex_trylock(pmutex_);
        if (rc != 0)
        {
            if (rc == EBUSY)
            {
                return false;
            }
            THROW_SYSTEM_ERROR_CODE("pthread_mutex_trylock()", rc);
        }

        return true;
    }


    LockGuard::LockGuard(Mutex& mutex)
        : mutex_(mutex)
    {
        mutex_.lock();
    }


    LockGuard::~LockGuard()
    {
        mutex_.unlock();
    }
}
