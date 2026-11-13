#include "OS/Mutex.h"

namespace kickcat
{
    // MSVC backend on a slim reader/writer lock, always taken exclusively. It is neither recursive
    // nor shareable between processes, and has no priority inheritance.
    Mutex::Mutex(os_mutex* mutex)
        : pmutex_(mutex)
        , mutex_()
    {
        if (mutex == nullptr)
        {
            pmutex_ = &mutex_;
            init();
        }
    }


    // An SRW lock owns no resource: nothing to release.
    Mutex::~Mutex() = default;


    void Mutex::init()
    {
        InitializeSRWLock(pmutex_);
    }


    void Mutex::lock()
    {
        AcquireSRWLockExclusive(pmutex_);
    }


    void Mutex::unlock()
    {
        ReleaseSRWLockExclusive(pmutex_);
    }


    bool Mutex::tryLock()
    {
        return TryAcquireSRWLockExclusive(pmutex_) != 0;
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


    TryLockGuard::TryLockGuard(Mutex& mutex)
        : mutex_(mutex)
        , owned_(mutex.tryLock())
    {
    }


    TryLockGuard::~TryLockGuard()
    {
        if (owned_)
        {
            mutex_.unlock();
        }
    }
}
