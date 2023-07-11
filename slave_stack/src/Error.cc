#include "kickcat/Error.h"

namespace kickcat
{
    char const* toString(hresult error)
    {
        switch (error)
        {
        case hresult::UNKNOWN_ERRNO: {return "Unknown errno";}
        case hresult::EACCES:        {return "Permission denied";}
        case hresult::EAGAIN:        {return "Operation would block";}
        case hresult::EBADF:         {return "Bad file descriptor";}
        case hresult::EBUSY:         {return "Device or resource busy";}
        case hresult::ECANCELED:     {return "Operation canceled";}
        case hresult::EDEADLK:       {return "Resource deadlock avoided";}
        case hresult::EINTR:         {return "Interrupted function call";}
        case hresult::EINVAL:        {return "Invalid argument";}
        case hresult::EMSGSIZE:      {return "Message too long";}
        case hresult::ENODEV:        {return "No such device";}
        case hresult::ENOENT:        {return "No such file or directory";}
        case hresult::ENOMEM:        {return "Not enough space/cannot allocate memory";}
        case hresult::ENOMSG:        {return "No message of the desired type";}
        case hresult::ENOSYS:        {return "Function not implemented";}
        case hresult::ENOTDIR:       {return "Not a directory";}
        case hresult::ENOTSOCK:      {return "Not a socket";}
        case hresult::ENOTSUP:       {return "Operation not supported";}
        case hresult::EOVERFLOW:     {return "Value too large to be stored in data type";}
        case hresult::EPROTO:        {return "Protocol error";}
        case hresult::EIO:           {return "Input/output error";}
        case hresult::ETIME:         {return "Timer expired";}
        case hresult::ETIMEDOUT:     {return "Timeout";}
        case hresult::ELOOP:         {return "Too many levels of symbolic links";}
        case hresult::EFAULT:        {return "Bad address";}
        case hresult::ENAMETOOLONG:  {return "Name too long";}
        case hresult::ESRCH:         {return "No such process";}
        case hresult::EPERM:         {return "Operation not permitted";}
        case hresult::ENXIO:         {return "No such device or address";}
        case hresult::E2BIG:         {return "Argument list too long";}
        case hresult::ECHILD:        {return "No child processes";}
        case hresult::EEXIST:        {return "File exists";}
        case hresult::EXDEV:         {return "Improper link";}
        case hresult::EISDIR:        {return "Is a directory";}
        case hresult::ENFILE:        {return "Too many open files in the system";}
        case hresult::EMFILE:        {return "Too many open files";}
        case hresult::ENOTTY:        {return "Inappropriate I/O control operation";}
        case hresult::ETXTBSY:       {return "Text file busy";}
        case hresult::EFBIG:         {return "File too large";}
        case hresult::ENOSPC:        {return "No space left on device";}
        case hresult::ESPIPE:        {return "Invalid seek";}
        case hresult::EROFS:         {return "Read-only filesystem";}
        case hresult::EMLINK:        {return "Too many links";}
        case hresult::EDOM:          {return "Mathematics argument out of domain of function";}
        case hresult::ERANGE:        {return "Out of bounds";}
        case hresult::ENOTEMPTY:     {return "Directory not empty";}
        case hresult::ENODATA:       {return "No message is available";}
        case hresult::EALREADY:      {return "Already in progress";}
        case hresult::EBADR:         {return "Invalid request descriptor";}
        case hresult::EBADE:         {return "Invalid exchange";}
        case hresult::EADDRINUSE:    {return "Address already in use";}
        case hresult::EOPNOTSUPP:    {return "Operation not supported";}
        case hresult::ENOTCONN:      {return "Not connected";}
        case hresult::ECONNRESET:    {return "Connection reset";}
        case hresult::ECONNREFUSED:  {return "Connection refused";}
        case hresult::EIDRM:         {return "Identifier removed";}
        case hresult::ENOSR:         {return "No stream resources";}
        case hresult::ENOSTR:        {return "Not a stream";}
        case hresult::EBADMSG:       {return "Bad message";}
        case hresult::EPIPE:         {return "Broken pipe";}
        case hresult::ECONNABORTED:  {return "Software caused connection abort";}
        case hresult::ENOBUFS:       {return "No buffer space available";}
        case hresult::EINPROGRESS:   {return "Operation in progress";}
        default: {return "Unknown Error";}
        }
    }
}
