#ifndef KICKCAT_OS_WINDOWS_OS_TYPES_H
#define KICKCAT_OS_WINDOWS_OS_TYPES_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

// We still use POSIX API whenever possible on Windows for now
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>

namespace kickcat
{
    using os_file   = int;
    using os_mutex  = pthread_mutex_t;
    using os_cond   = pthread_cond_t;
    using os_sem    = sem_t;
    using os_shm    = HANDLE;
    using os_socket = int;
    using os_socket_context = [[maybe_unused]] int;
    using os_thread = pthread_t;
    using os_thread_context = [[maybe_unused]] int;
    using os_pid    = pid_t;
}

#endif
