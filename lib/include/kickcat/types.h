#ifndef KICKCAT_TYPES_H
#define KICKCAT_TYPES_H

#if defined(__linux__) || defined(__NuttX__)
#include "OS/Linux/types/os_types.h"
#elif __PikeOS__
#include "OS/PikeOS/types/os_types.h"
#endif

#endif
