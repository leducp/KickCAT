#include <cstdio>
#include <system_error>

#include <pthread.h>
#include <sched.h>

#include "kickcat/OS/Thread.h"

namespace kickcat
{
    Thread::Thread(std::string_view name, ThreadFunction routine, int priority)
        : name_{name}
        , thread_{}
        , routine_{routine}
        , priority_{priority}
    {
    }

    Thread::Thread(std::string_view name, std::function<void()> routine, int priority)
        : name_{name}
        , thread_{}
        , routine_{callable_trampoline}
        , callable_{std::move(routine)}
        , priority_{priority}
    {
    }

    void* Thread::callable_trampoline(void* self)
    {
        static_cast<Thread*>(self)->callable_();
        return nullptr;
    }

    void Thread::start()
    {
        start(this);
    }

    void Thread::start(void* args)
    {
        pthread_attr_t attribute;
        int rc = pthread_attr_init(&attribute);
        if (0 != rc)
        {
            throw std::system_error(rc, std::system_category());
        }

        // Use the attribute scheduling priority instead of inheriting it from the creating thread.
        rc = pthread_attr_setinheritsched(&attribute, PTHREAD_EXPLICIT_SCHED);
        if (rc != 0)
        {
            throw std::system_error(rc, std::system_category());
        }

        // priority > 0 -> SCHED_FIFO (real-time); priority == 0 -> SCHED_OTHER (time-shared)
        struct sched_param params;
        params.sched_priority = priority_;
        int policy = SCHED_FIFO;
        if (priority_ <= 0)
        {
            policy = SCHED_OTHER;
        }
        rc = pthread_attr_setschedpolicy(&attribute, policy);
        if (rc != 0)
        {
            throw std::system_error(rc, std::system_category());
        }

        rc = pthread_attr_setschedparam(&attribute, &params);
        if (rc != 0)
        {
            throw std::system_error(rc, std::system_category());
        }

        rc = pthread_create(&thread_, &attribute, routine_, args);
        if (rc != 0)
        {
            if (rc == EPERM)
            {
                std::fprintf(stderr,
                    "| Thread creation is forbidden: ensure you have the right to launch real-time threads (prio > 0).\n"
                    "| Grant it, e.g. 'sudo setcap cap_sys_nice+ep <exe>' or an rtprio ulimit.\n");
            }
            throw std::system_error(rc, std::system_category());
        }

        rc = pthread_attr_destroy(&attribute);
        if (rc)
        {
            throw std::system_error(rc, std::system_category());
        }

        is_valid_ = true;
    }

    void Thread::join()
    {
        if (not is_valid_)
        {
            throw std::system_error(ESRCH, std::system_category());
        }

        int const rc = pthread_join(thread_, nullptr);
        if (rc != 0)
        {
            throw std::system_error(rc, std::system_category());
        }
    }

    void Thread::cancel()
    {
        if (not is_valid_)
        {
            throw std::system_error(ESRCH, std::system_category());
        }

        int const rc = pthread_cancel(thread_);
        if (rc != 0)
        {
            throw std::system_error(rc, std::system_category());
        }
    }

    std::string_view Thread::name() const
    {
        return name_;
    }

    os_thread Thread::self()
    {
        return thread_;
    }

    int Thread::priority(os_thread thread)
    {
        int policy;
        struct sched_param param;
        int rc = pthread_getschedparam(thread, &policy, &param);
        if (rc != 0)
        {
            throw std::system_error(rc, std::system_category());
        }
        return param.sched_priority;
    }

    int Thread::policy(os_thread thread)
    {
        int policy;
        struct sched_param param;
        int rc = pthread_getschedparam(thread, &policy, &param);
        if (rc != 0)
        {
            throw std::system_error(rc, std::system_category());
        }
        return policy;
    }

    void Thread::set_priority(int priority, os_thread thread)
    {
        int policy = SCHED_FIFO;
        if (priority <= 0)
        {
            policy = SCHED_OTHER;
        }

        struct sched_param params;
        params.sched_priority = priority;
        int rc = pthread_setschedparam(thread, policy, &params);
        if (rc != 0)
        {
            throw std::system_error(rc, std::system_category());
        }
    }

    int Thread::min_priority()
    {
        int min_priority = sched_get_priority_min(SCHED_FIFO);
        if (min_priority < 0)
        {
            throw std::system_error(errno, std::system_category());
        }
        return min_priority;
    }

    int Thread::max_priority()
    {
        int const max_priority = sched_get_priority_max(SCHED_FIFO);
        if (max_priority < 0)
        {
            throw std::system_error(errno, std::system_category());
        }
        return max_priority;
    }

    void Thread::set_affinity(int cpu, os_thread thread)
    {
#if defined(__linux__)
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(cpu, &cpuset);
        int rc = pthread_setaffinity_np(thread, sizeof(cpuset), &cpuset);
        if (rc != 0)
        {
            throw std::system_error(rc, std::system_category());
        }
#else
        (void) cpu;
        (void) thread;
        throw std::system_error(ENOSYS, std::system_category(), "set_affinity not supported");
#endif
    }

    void Thread::yield()
    {
        sched_yield();
    }
}
