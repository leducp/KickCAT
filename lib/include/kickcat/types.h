#ifndef KICKCAT_TYPES_H
#define KICKCAT_TYPES_H

// KICKOS first: the KickOS build also defines __unix__ when compiled on a host.
#if defined(KICKOS)
#include "OS/KickOS/types/os_types.h"
#elif defined(__unix__) || defined(__NuttX__)
#include "OS/Unix/types/os_types.h"
#elif defined(__MINGW64__ )
#include "OS/Windows/types/os_types.h"
#elif __PikeOS__
#include "OS/PikeOS/types/os_types.h"
#endif

#endif
