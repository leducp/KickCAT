#ifndef KICKCAT_LOCKED_RING_H
#define KICKCAT_LOCKED_RING_H

#include "kickcat/Ring.h"
#include "kickcat/OS/ConditionVariable.h"
#include "kickcat/OS/Mutex.h"
#include "kickcat/OS/Time.h"

namespace kickcat
{
    // A bounded SPSC queue: a fixed-capacity Ring + Mutex + ConditionVariable.
    // Storage and sync primitives live in a caller-owned Context -- private (stack
    // or member) or in shared memory (see SBufQueue).
    template<typename T, uint32_t N>
    class LockedRing
    {
    public:
        struct Context
        {
            os_mutex lock;
            os_cond  cond;
            typename Ring<T, N>::Context ring;
        };

        LockedRing(Context& ctx)
            : lock_{ctx.lock}
            , cond_{ctx.cond}
            , ring_{ctx.ring}
        {
        }

        // Call once before use: the Context is raw (possibly shared) storage.
        void init()
        {
            lock_.init();
            cond_.init();
            ring_.reset();
        }

        // false when full.
        bool push(T const& value)
        {
            // signal under the lock: signalling after unlock can race a waiter into
            // an empty ring.
            LockGuard guard(lock_);
            if (not ring_.push(value))
            {
                return false;
            }
            cond_.signal();
            return true;
        }

        // false when empty.
        bool tryPop(T& out)
        {
            LockGuard guard(lock_);
            return ring_.pop(out);
        }

        // Block up to `timeout` (negative = forever); false on timeout.
        bool popWait(T& out, nanoseconds timeout)
        {
            LockGuard guard(lock_);
            auto ready = [this]{ return not ring_.isEmpty(); };
            if (timeout < 0ns)
            {
                cond_.wait(lock_, ready);
            }
            else if (cond_.wait_until(lock_, timeout + 1ms, ready) != 0)
            {
                return false;
            }
            return ring_.pop(out);
        }

        uint32_t size()
        {
            LockGuard guard(lock_);
            return ring_.size();
        }

    private:
        Mutex             lock_;
        ConditionVariable cond_;
        Ring<T, N>        ring_;
    };
}

#endif
