#ifndef KICKCAT_TYPES_H
#define KICKCAT_TYPES_H

#if defined(__unix__) || defined(__NuttX__)
#include "OS/Unix/types/os_types.h"
#elif __PikeOS__
#include "OS/PikeOS/types/os_types.h"
#endif

#endif
