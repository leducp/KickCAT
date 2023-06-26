#include "kickcat/Error.h"

namespace kickcat
{
    char* toString(ErrorCode error)
    {
        switch (error)
        {
        case ErrorCode::UNKNOWN_ERRNO: {return "Unknown errno";}
        case ErrorCode::EACCES:        {return "Permission denied";}
        case ErrorCode::EAGAIN:        {return "Operation would block";}
        case ErrorCode::EBADF:         {return "Bad file descriptor";}
        case ErrorCode::EBUSY:         {return "Device or resource busy";}
        case ErrorCode::ECANCELED:     {return "Operation canceled";}
        case ErrorCode::EDEADLK:       {return "Resource deadlock avoided";}
        case ErrorCode::EINTR:         {return "Interrupted function call";}
        case ErrorCode::EINVAL:        {return "Invalid argument";}
        case ErrorCode::EMSGSIZE:      {return "Message too long";}
        case ErrorCode::ENODEV:        {return "No such device";}
        case ErrorCode::ENOENT:        {return "No such file or directory";}
        case ErrorCode::ENOMEM:        {return "Not enough space/cannot allocate memory";}
        case ErrorCode::ENOMSG:        {return "No message of the desired type";}
        case ErrorCode::ENOSYS:        {return "Function not implemented";}
        case ErrorCode::ENOTDIR:       {return "Not a directory";}
        case ErrorCode::ENOTSOCK:      {return "Not a socket";}
        case ErrorCode::ENOTSUP:       {return "Operation not supported";}
        case ErrorCode::EOVERFLOW:     {return "Value too large to be stored in data type";}
        case ErrorCode::EPROTO:        {return "Protocol error";}
        case ErrorCode::EIO:           {return "Input/output error";}
        case ErrorCode::ETIME:         {return "Timer expired";}
        case ErrorCode::ETIMEDOUT:     {return "Timeout";}
        case ErrorCode::ELOOP:         {return "Too many levels of symbolic links";}
        case ErrorCode::EFAULT:        {return "Bad address";}
        case ErrorCode::ENAMETOOLONG:  {return "Name too long";}
        case ErrorCode::ESRCH:         {return "No such process";}
        case ErrorCode::EPERM:         {return "Operation not permitted";}
        case ErrorCode::ENXIO:         {return "No such device or address";}
        case ErrorCode::E2BIG:         {return "Argument list too long";}
        case ErrorCode::ECHILD:        {return "No child processes";}
        case ErrorCode::EEXIST:        {return "File exists";}
        case ErrorCode::EXDEV:         {return "Improper link";}
        case ErrorCode::EISDIR:        {return "Is a directory";}
        case ErrorCode::ENFILE:        {return "Too many open files in the system";}
        case ErrorCode::EMFILE:        {return "Too many open files";}
        case ErrorCode::ENOTTY:        {return "Inappropriate I/O control operation";}
        case ErrorCode::ETXTBSY:       {return "Text file busy";}
        case ErrorCode::EFBIG:         {return "File too large";}
        case ErrorCode::ENOSPC:        {return "No space left on device";}
        case ErrorCode::ESPIPE:        {return "Invalid seek";}
        case ErrorCode::EROFS:         {return "Read-only filesystem";}
        case ErrorCode::EMLINK:        {return "Too many links";}
        case ErrorCode::EDOM:          {return "Mathematics argument out of domain of function";}
        case ErrorCode::ERANGE:        {return "Out of bounds";}
        case ErrorCode::ENOTEMPTY:     {return "Directory not empty";}
        case ErrorCode::ENODATA:       {return "No message is available";}
        case ErrorCode::EALREADY:      {return "Already in progress";}
        case ErrorCode::EBADR:         {return "Invalid request descriptor";}
        case ErrorCode::EBADE:         {return "Invalid exchange";}
        case ErrorCode::EADDRINUSE:    {return "Address already in use";}
        case ErrorCode::EOPNOTSUPP:    {return "Operation not supported";}
        case ErrorCode::ENOTCONN:      {return "Not connected";}
        case ErrorCode::ECONNRESET:    {return "Connection reset";}
        case ErrorCode::ECONNREFUSED:  {return "Connection refused";}
        case ErrorCode::EIDRM:         {return "Identifier removed";}
        case ErrorCode::ENOSR:         {return "No stream resources";}
        case ErrorCode::ENOSTR:        {return "Not a stream";}
        case ErrorCode::EBADMSG:       {return "Bad message";}
        case ErrorCode::EPIPE:         {return "Broken pipe";}
        case ErrorCode::ECONNABORTED:  {return "Software caused connection abort";}
        case ErrorCode::ENOBUFS:       {return "No buffer space available";}
        case ErrorCode::EINPROGRESS:   {return "Operation in progress";}
        default: {return "Unknown Error";}
        }
    }
}
