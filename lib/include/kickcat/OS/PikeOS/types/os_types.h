#ifndef KICKCAT_OS_PIKEOS_OS_TYPES_H
#define KICKCAT_OS_PIKEOS_OS_TYPES_H

// Define standard C exit types
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

extern "C"
{
    #include <p4.h>
    #include <p4ext_threads.h>
    #include <vm.h>
    #include <vm_io_sbuf.h>
}

namespace kickcat
{
    using os_file   = vm_file_desc_t;
    using os_mutex  = P4_mutex_t;
    using os_cond   = P4_cond_t;
    using os_sem    = P4_sem_t;
    using os_shm    = vm_file_desc_t;
    using os_socket = vm_file_desc_t;
    using os_thread = P4_thr_t;
    constexpr os_thread thread_self() { return P4_THREAD_MYSELF; }
    using os_pid    = vm_part_id_t;
}

#endif
