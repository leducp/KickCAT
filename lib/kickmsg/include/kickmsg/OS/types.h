#ifndef KICKMSG_OS_TYPES_H
#define KICKMSG_OS_TYPES_H

#if defined(__unix__) || defined(__APPLE__)
#include "Unix/types/os_types.h"
#else
#error "KickMsg: unsupported platform"
#endif

#endif // KICKMSG_OS_TYPES_H
