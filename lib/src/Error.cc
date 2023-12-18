#include "kickcat/Error.h"

namespace kickcat
{
    char const* toString(hresult error)
    {
        switch (error)
        {
        case hresult::E_UNKNOWN_ERRNO: {return "Unknown errno";}
        case hresult::E_EACCES:        {return "Permission denied";}
        case hresult::E_EAGAIN:        {return "Operation would block";}
        case hresult::E_EBADF:         {return "Bad file descriptor";}
        case hresult::E_EBUSY:         {return "Device or resource busy";}
        case hresult::E_ECANCELED:     {return "Operation canceled";}
        case hresult::E_EDEADLK:       {return "Resource deadlock avoided";}
        case hresult::E_EINTR:         {return "Interrupted function call";}
        case hresult::E_EINVAL:        {return "Invalid argument";}
        case hresult::E_EMSGSIZE:      {return "Message too long";}
        case hresult::E_ENODEV:        {return "No such device";}
        case hresult::E_ENOENT:        {return "No such file or directory";}
        case hresult::E_ENOMEM:        {return "Not enough space/cannot allocate memory";}
        case hresult::E_ENOMSG:        {return "No message of the desired type";}
        case hresult::E_ENOSYS:        {return "Function not implemented";}
        case hresult::E_ENOTDIR:       {return "Not a directory";}
        case hresult::E_ENOTSOCK:      {return "Not a socket";}
        case hresult::E_ENOTSUP:       {return "Operation not supported";}
        case hresult::E_EOVERFLOW:     {return "Value too large to be stored in data type";}
        case hresult::E_EPROTO:        {return "Protocol error";}
        case hresult::E_EIO:           {return "Input/output error";}
        case hresult::E_ETIME:         {return "Timer expired";}
        case hresult::E_ETIMEDOUT:     {return "Timeout";}
        case hresult::E_ELOOP:         {return "Too many levels of symbolic links";}
        case hresult::E_EFAULT:        {return "Bad address";}
        case hresult::E_ENAMETOOLONG:  {return "Name too long";}
        case hresult::E_ESRCH:         {return "No such process";}
        case hresult::E_EPERM:         {return "Operation not permitted";}
        case hresult::E_ENXIO:         {return "No such device or address";}
        case hresult::E_E2BIG:         {return "Argument list too long";}
        case hresult::E_ECHILD:        {return "No child processes";}
        case hresult::E_EEXIST:        {return "File exists";}
        case hresult::E_EXDEV:         {return "Improper link";}
        case hresult::E_EISDIR:        {return "Is a directory";}
        case hresult::E_ENFILE:        {return "Too many open files in the system";}
        case hresult::E_EMFILE:        {return "Too many open files";}
        case hresult::E_ENOTTY:        {return "Inappropriate I/O control operation";}
        case hresult::E_ETXTBSY:       {return "Text file busy";}
        case hresult::E_EFBIG:         {return "File too large";}
        case hresult::E_ENOSPC:        {return "No space left on device";}
        case hresult::E_ESPIPE:        {return "Invalid seek";}
        case hresult::E_EROFS:         {return "Read-only filesystem";}
        case hresult::E_EMLINK:        {return "Too many links";}
        case hresult::E_EDOM:          {return "Mathematics argument out of domain of function";}
        case hresult::E_ERANGE:        {return "Out of bounds";}
        case hresult::E_ENOTEMPTY:     {return "Directory not empty";}
        case hresult::E_ENODATA:       {return "No message is available";}
        case hresult::E_EALREADY:      {return "Already in progress";}
        case hresult::E_EBADR:         {return "Invalid request descriptor";}
        case hresult::E_EBADE:         {return "Invalid exchange";}
        case hresult::E_EADDRINUSE:    {return "Address already in use";}
        case hresult::E_EOPNOTSUPP:    {return "Operation not supported";}
        case hresult::E_ENOTCONN:      {return "Not connected";}
        case hresult::E_ECONNRESET:    {return "Connection reset";}
        case hresult::E_ECONNREFUSED:  {return "Connection refused";}
        case hresult::E_EIDRM:         {return "Identifier removed";}
        case hresult::E_ENOSR:         {return "No stream resources";}
        case hresult::E_ENOSTR:        {return "Not a stream";}
        case hresult::E_EBADMSG:       {return "Bad message";}
        case hresult::E_EPIPE:         {return "Broken pipe";}
        case hresult::E_ECONNABORTED:  {return "Software caused connection abort";}
        case hresult::E_ENOBUFS:       {return "No buffer space available";}
        case hresult::E_EINPROGRESS:   {return "Operation in progress";}
        default: {return "Unknown Error";}
        }
    }
}
