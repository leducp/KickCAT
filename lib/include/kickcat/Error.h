#ifndef KICKCAT_ERROR_H
#define KICKCAT_ERROR_H

#include <exception>
#include <system_error>

#include "KickCAT.h"

namespace kickcat
{
    constexpr const char* strip_path(const char* path)
    {
        const char* file = path;
        while (*path)
        {
            if (*path++ == '/')
            {
                file = path;
            }
            if (*path == ':')
            {
                break;
            }
        }
        return file;
    }

    #define STR1(x) #x
    #define STR2(x) STR1(x)
    #define LOCATION(suffix) kickcat::strip_path(__FILE__ ":" STR2(__LINE__) suffix)
    #define THROW_ERROR(msg)                    (throw kickcat::Error{LOCATION(": " msg)})
    #define THROW_ERROR_CODE(msg, code)         (throw kickcat::ErrorCode{LOCATION(": " msg), static_cast<int32_t>(code)})
    #define THROW_ERROR_DATAGRAM(msg, state)    (throw kickcat::ErrorDatagram{LOCATION(": " msg), state})
    #define THROW_SYSTEM_ERROR_CODE(msg, code)  (throw std::system_error(code, std::generic_category(), LOCATION(": " msg)))
    #define THROW_SYSTEM_ERROR(msg)             THROW_SYSTEM_ERROR_CODE(msg, errno)


    struct Error : public std::exception
    {
        Error(char const* message)
            : message_(message)
        { }

        char const* what() const noexcept override
        {
            return message_;
        }

    private:
        char const* message_;
    };

    struct ErrorCode : public Error
    {
        ErrorCode(char const* message, int32_t code)
            : Error(message)
            , code_{code}
        { }

        int32_t code() const noexcept
        {
            return code_;
        }

    private:
        int32_t code_;
    };

    struct ErrorDatagram : public Error
    {
        ErrorDatagram(char const* message, DatagramState state)
        : Error(message)
        {
            state_ = state;
        }

        DatagramState state() const noexcept
        {
            return state_;
        }

    private:
        DatagramState state_;
    };


    // TODO replace with exceptions / errno
        // Errno codes:
    enum class hresult: uint32_t
    {
        OK = 0,
        E_UNKNOWN_ERRNO = 1,  // "Unknown errno"
        E_EACCES        = 2,  // "Permission denied"
        E_EAGAIN        = 3,  // "Operation would block"
        E_EBADF         = 4,  // "Bad file descriptor"
        E_EBUSY         = 5,  // "Device or resource busy"
        E_ECANCELED     = 6,  // "Operation canceled"
        E_EDEADLK       = 7,  // "Resource deadlock avoided"
        E_EINTR         = 8,  // "Interrupted function call"
        E_EINVAL        = 9,  // "Invalid argument"
        E_EMSGSIZE      = 10, // "Message too long"
        E_ENODEV        = 11, // "No such device"
        E_ENOENT        = 12, // "No such file or directory"
        E_ENOMEM        = 13, // "Not enough space/cannot allocate memory"
        E_ENOMSG        = 14, // "No message of the desired type"
        E_ENOSYS        = 15, // "Function not implemented"
        E_ENOTDIR       = 16, // "Not a directory"
        E_ENOTSOCK      = 17, // "Not a socket"
        E_ENOTSUP       = 18, // "Operation not supported"
        E_EOVERFLOW     = 19, // "Value too large to be stored in data type"
        E_EPROTO        = 20, // "Protocol error"
        E_EIO           = 21, // "Input/output error"
        E_ETIME         = 22, // "Timer expired"
        E_ETIMEDOUT     = 23, // "Timeout"
        E_ELOOP         = 24, // "Too many levels of symbolic links"
        E_EFAULT        = 25, // "Bad address"
        E_ENAMETOOLONG  = 26, // "Name too long"
        E_ESRCH         = 27, // "No such process"
        E_EPERM         = 28, // "Operation not permitted"
        E_ENXIO         = 29, // "No such device or address"
        E_E2BIG         = 30, // "Argument list too long"
        E_ECHILD        = 31, // "No child processes"
        E_EEXIST        = 32, // "File exists"
        E_EXDEV         = 33, // "Improper link"
        E_EISDIR        = 34, // "Is a directory"
        E_ENFILE        = 35, // "Too many open files in the system"
        E_EMFILE        = 36, // "Too many open files"
        E_ENOTTY        = 37, // "Inappropriate I/O control operation"
        E_ETXTBSY       = 38, // "Text file busy"
        E_EFBIG         = 39, // "File too large"
        E_ENOSPC        = 40, // "No space left on device"
        E_ESPIPE        = 41, // "Invalid seek"
        E_EROFS         = 42, // "Read-only filesystem"
        E_EMLINK        = 43, // "Too many links"
        E_EDOM          = 44, // "Mathematics argument out of domain of function"
        E_ERANGE        = 45, // "Out of bounds"
        E_ENOTEMPTY     = 46, // "Directory not empty"
        E_ENODATA       = 47, // "No message is available"
        E_EALREADY      = 48, // "Already in progress"
        E_EBADR         = 49, // "Invalid request descriptor"
        E_EBADE         = 50, // "Invalid exchange"
        E_EADDRINUSE    = 51, // "Address already in use"
        E_EOPNOTSUPP    = 52, // "Operation not supported"
        E_ENOTCONN      = 53, // "Not connected"
        E_ECONNRESET    = 54, // "Connection reset"
        E_ECONNREFUSED  = 55, // "Connection refused"
        E_EIDRM         = 56, // "Identifier removed"
        E_ENOSR         = 57, // "No stream resources"
        E_ENOSTR        = 58, // "Not a stream"
        E_EBADMSG       = 59, // "Bad message"
        E_EPIPE         = 60, // "Broken pipe"
        E_ECONNABORTED  = 61, // "Software caused connection abort"
        E_ENOBUFS       = 62, // "No buffer space available"
        E_EINPROGRESS   = 63, // "Operation in progress"
    };

    char const* toString(hresult error);
}

#endif
