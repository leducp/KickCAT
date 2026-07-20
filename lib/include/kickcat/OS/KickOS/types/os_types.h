#ifndef KICKCAT_OS_KICKOS_OS_TYPES_H
#define KICKCAT_OS_KICKOS_OS_TYPES_H

namespace kickcat
{
    // Placeholder handles for the not-yet-implemented KickOS IPC backend: Mutex,
    // ConditionVariable and SharedMemory throw if used (see OS/KickOS/*.cc). Only the
    // types referenced by code compiled for KickOS are defined here.
    using os_mutex = int;
    using os_cond  = int;
    using os_shm   = int;
}

#endif
