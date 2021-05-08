#ifndef KICKCAT_ERROR_H
#define KICKCAT_ERROR_H

#include <exception>
#include <system_error>

namespace kickcat
{
    #define STR1(x) #x
    #define STR2(x) STR1(x)
    #define LOCATION __FILE__ ":" STR2(__LINE__)
    #define THROW_ERROR(msg)            (throw Error{LOCATION ": " msg})
    #define THROW_ERROR_CODE(msg, code) (throw ErrorCode{LOCATION ": " msg, static_cast<int32_t>(code)})
    #define THROW_SYSTEM_ERROR(msg)     (throw std::system_error(errno, std::generic_category(), LOCATION ": " msg))

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
}

#endif
