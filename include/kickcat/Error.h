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

#ifdef DEBUG
    #define DEBUG_PRINT(...) do { fprintf(stderr, "DEBUG: %s ", LOCATION()); fprintf(stderr, ##__VA_ARGS__); } while(0);
#else
    #define DEBUG_PRINT(...)
#endif

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
}

#endif
