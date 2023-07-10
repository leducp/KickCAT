#ifndef SLAVE_STACK_KICKCAT_ERROR_H_
#define SLAVE_STACK_KICKCAT_ERROR_H_

#include <cstdint>

namespace kickcat
{
    // Errno codes:
    enum hresult: uint32_t
    {
        OK = 0,
        UNKNOWN_ERRNO = 1,  // "Unknown errno"
        EACCES        = 2,  // "Permission denied"
        EAGAIN        = 3,  // "Operation would block"
        EBADF         = 4,  // "Bad file descriptor"
        EBUSY         = 5,  // "Device or resource busy"
        ECANCELED     = 6,  // "Operation canceled"
        EDEADLK       = 7,  // "Resource deadlock avoided"
        EINTR         = 8,  // "Interrupted function call"
        EINVAL        = 9,  // "Invalid argument"
        EMSGSIZE      = 10, // "Message too long"
        ENODEV        = 11, // "No such device"
        ENOENT        = 12, // "No such file or directory"
        ENOMEM        = 13, // "Not enough space/cannot allocate memory"
        ENOMSG        = 14, // "No message of the desired type"
        ENOSYS        = 15, // "Function not implemented"
        ENOTDIR       = 16, // "Not a directory"
        ENOTSOCK      = 17, // "Not a socket"
        ENOTSUP       = 18, // "Operation not supported"
        EOVERFLOW     = 19, // "Value too large to be stored in data type"
        EPROTO        = 20, // "Protocol error"
        EIO           = 21, // "Input/output error"
        ETIME         = 22, // "Timer expired"
        ETIMEDOUT     = 23, // "Timeout"
        ELOOP         = 24, // "Too many levels of symbolic links"
        EFAULT        = 25, // "Bad address"
        ENAMETOOLONG  = 26, // "Name too long"
        ESRCH         = 27, // "No such process"
        EPERM         = 28, // "Operation not permitted"
        ENXIO         = 29, // "No such device or address"
        E2BIG         = 30, // "Argument list too long"
        ECHILD        = 31, // "No child processes"
        EEXIST        = 32, // "File exists"
        EXDEV         = 33, // "Improper link"
        EISDIR        = 34, // "Is a directory"
        ENFILE        = 35, // "Too many open files in the system"
        EMFILE        = 36, // "Too many open files"
        ENOTTY        = 37, // "Inappropriate I/O control operation"
        ETXTBSY       = 38, // "Text file busy"
        EFBIG         = 39, // "File too large"
        ENOSPC        = 40, // "No space left on device"
        ESPIPE        = 41, // "Invalid seek"
        EROFS         = 42, // "Read-only filesystem"
        EMLINK        = 43, // "Too many links"
        EDOM          = 44, // "Mathematics argument out of domain of function"
        ERANGE        = 45, // "Out of bounds"
        ENOTEMPTY     = 46, // "Directory not empty"
        ENODATA       = 47, // "No message is available"
        EALREADY      = 48, // "Already in progress"
        EBADR         = 49, // "Invalid request descriptor"
        EBADE         = 50, // "Invalid exchange"
        EADDRINUSE    = 51, // "Address already in use"
        EOPNOTSUPP    = 52, // "Operation not supported"
        ENOTCONN      = 53, // "Not connected"
        ECONNRESET    = 54, // "Connection reset"
        ECONNREFUSED  = 55, // "Connection refused"
        EIDRM         = 56, // "Identifier removed"
        ENOSR         = 57, // "No stream resources"
        ENOSTR        = 58, // "Not a stream"
        EBADMSG       = 59, // "Bad message"
        EPIPE         = 60, // "Broken pipe"
        ECONNABORTED  = 61, // "Software caused connection abort"
        ENOBUFS       = 62, // "No buffer space available"
        EINPROGRESS   = 63, // "Operation in progress"
    };

    char const* toString(hresult error);
}




#endif
