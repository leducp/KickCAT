#ifndef KICKCAT_OS_THREAD_H
#define KICKCAT_OS_THREAD_H

#include <functional>
#include <string>
#include <string_view>

#include "kickcat/types.h"

namespace kickcat
{
    // A wrapper around the OS thread mechanisms.
    // This class takes a function that will be started in a dedicated thread.
    using ThreadFunction = void* (*)(void*);
    class Thread
    {
    public:
        /// \param name     name of the thread
        /// \param routine  C-style function to run in a new thread
        /// \param priority 0 -> SCHED_OTHER (time-shared), 1-N -> SCHED_FIFO real-time (higher = greater)
        Thread(std::string_view name, ThreadFunction routine, int priority);

        /// \param name     name of the thread
        /// \param routine  callable (lambda, std::function, ...) to run in a new thread
        /// \param priority 0 -> SCHED_OTHER (time-shared), 1-N -> SCHED_FIFO real-time (higher = greater)
        Thread(std::string_view name, std::function<void()> routine, int priority);

        ~Thread() = default;

        std::string_view name() const;

        // create and start the thread (C-style: pass args to the stored ThreadFunction)
        void start(void* args);

        // create and start the thread (callable: invokes the stored std::function)
        void start();

        // Wait for the thread to terminate.
        void join();

        // Cancel the thread (deferred: the thread must reach a cancelation point).
        void cancel();

        // causes the calling thread to relinquish the CPU
        static void yield();

        // get thread OS handle to manipulate it
        os_thread self();

        // Retrieve the thread current scheduling policy
        static int policy(os_thread tid);

        // Return minimal priority of RT scheduling policy
        static int min_priority();

        // Return maximal priority of RT scheduling policy
        static int max_priority();

        // positive -> real time (FIFO), 0 -> no real time, default OS parameter
        static void set_priority(int priority, os_thread thread = thread_self());

        // get thread current priority
        static int priority(os_thread tid);

        /// Pin thread to a single CPU (Linux only). Throws on other platforms.
        static void set_affinity(int cpu, os_thread thread = thread_self());

    private:
        static void* callable_trampoline(void* self);

        std::string name_;
        os_thread thread_;
        ThreadFunction routine_{nullptr};
        std::function<void()> callable_;
        int priority_;
        bool is_valid_{false};
    };
}

#endif
