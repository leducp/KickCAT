#include <gtest/gtest.h>

#include "kickcat/OS/Mutex.h"

using namespace kickcat;

TEST(TryLockGuard, acquires_a_free_mutex)
{
    Mutex m;
    TryLockGuard guard(m);
    EXPECT_TRUE(guard.owns());
}

TEST(TryLockGuard, fails_on_a_held_mutex)
{
    Mutex m;
    m.lock();
    {
        TryLockGuard guard(m);
        EXPECT_FALSE(guard.owns());   // already held: non-blocking attempt fails
    }
    m.unlock();
}

TEST(TryLockGuard, releases_on_scope_exit)
{
    Mutex m;
    {
        TryLockGuard guard(m);
        ASSERT_TRUE(guard.owns());
    }
    // The guard owned the lock and must have released it: a fresh attempt succeeds.
    TryLockGuard again(m);
    EXPECT_TRUE(again.owns());
}

TEST(TryLockGuard, does_not_release_a_lock_it_never_took)
{
    Mutex m;
    m.lock();
    {
        TryLockGuard guard(m);
        EXPECT_FALSE(guard.owns());
    }   // must NOT unlock m here -- it never owned it
    // m is still held by us; releasing it is the only valid next step.
    m.unlock();
    TryLockGuard guard(m);
    EXPECT_TRUE(guard.owns());
}
