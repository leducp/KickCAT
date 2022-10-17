#ifndef KICKCAT_ERROR_H
#define KICKCAT_ERROR_H

#include <exception>
#include <system_error>

#include "KickCAT.h"

namespace kickcat
{
    #define STR1(x) #x
    #define STR2(x) STR1(x)
    #define LOCATION __FILE__ ":" STR2(__LINE__)
    #define THROW_ERROR(msg)                 (throw Error{LOCATION ": " msg, msg})
    #define THROW_ERROR_CODE(msg, code)      (throw ErrorCode{LOCATION ": " msg, static_cast<int32_t>(code)})
    #define THROW_ERROR_DATAGRAM(msg, state) (throw ErrorDatagram{LOCATION ": " msg, state})
    #define THROW_SYSTEM_ERROR(msg)          (throw std::system_error(errno, std::generic_category(), LOCATION ": " msg))

//#define DEBUG 1
#ifdef DEBUG
    #define DEBUG_PRINT(...) do { fprintf(stderr, "DEBUG: %s:%d: ", __FILE__, __LINE__); fprintf(stderr, ##__VA_ARGS__); } while(0);
#else
    #define DEBUG_PRINT(...)
#endif

    struct Error : public std::exception
    {
        Error(char const* message, char const* m) : message_(std::move(message)), msg_(std::move(m)) {};
        Error(char const* message)
            : message_(message)
        { }

        char const* what() const noexcept override
        {
            return message_;
        }

        char const* msg() const 
        {
            return msg_;
        }

    private:
        char const* message_;
        const char* msg_;
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
