#ifndef KICKCAT_OS_UNIX_OS_TYPES_H
#define KICKCAT_OS_UNIX_OS_TYPES_H

#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>

namespace kickcat
{
    using os_file   = int;
    using os_mutex  = pthread_mutex_t;
    using os_cond   = pthread_cond_t;
    using os_sem    = sem_t;
    using os_shm    = int;
    using os_socket = int;
    using os_thread = pthread_t;
    using os_pid    = pid_t;
}

#ifdef __NuttX__
    #define thread_self pthread_self
#else
namespace kickcat
{
    constexpr auto thread_self = pthread_self;
}
#endif

#endif
