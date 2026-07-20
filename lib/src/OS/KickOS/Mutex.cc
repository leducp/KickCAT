// KickOS Mutex backend: not implemented yet. Every operation throws so the full library
// links for KickOS while the real primitive is written. Construction does not throw, so a
// Mutex may be held as a member; the throw happens on first use.
#include "OS/Mutex.h"
#include "Error.h"

// Throw-only placeholders: -Wmissing-noreturn is expected until the real backend lands.
#pragma GCC diagnostic ignored "-Wmissing-noreturn"

namespace kickcat
{
    Mutex::Mutex(os_mutex* mutex)
        : pmutex_(mutex)
        , mutex_(0)
    {
        if (mutex == nullptr)
        {
            pmutex_ = &mutex_;
        }
    }

    Mutex::~Mutex() = default;

    void Mutex::init()
    {
        THROW_ERROR("Mutex::init() not implemented on KickOS");
    }

    void Mutex::lock()
    {
        THROW_ERROR("Mutex::lock() not implemented on KickOS");
    }

    void Mutex::unlock()
    {
        THROW_ERROR("Mutex::unlock() not implemented on KickOS");
    }

    bool Mutex::tryLock()
    {
        THROW_ERROR("Mutex::tryLock() not implemented on KickOS");
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
