
#include <cstring>
#include <cstdio>
#include <errno.h>

// xpg_strerror: XPG-style strerror wrapper
// Uses GNU strerror internally, but provides thread-safe semantics
// by copying into a thread-local buffer.
// To use if the symbol is not defined (i.e. NuttX + GCC 14 ARM toolchain)
extern "C" int __xpg_strerror_r(int errnum, char *buf, size_t buflen)
{
    if (buf == nullptr or buflen == 0)
    {
        return EINVAL;
    }

    char const* msg = strerror(errnum);
    if (!msg)
    {
        snprintf(buf, buflen, "Unknown error %d", errnum);
        return EINVAL;
    }

    // Copy safely into the user-provided buffer
    strncpy(buf, msg, buflen - 1);
    buf[buflen - 1] = '\0';

    return 0;
}
