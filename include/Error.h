#ifndef KICKCAT_ERROR_H
#define KICKCAT_ERROR_H

#include <cstdint>
#include <string_view>
#include <array>

namespace kickcat
{
    #define ESUCCESS Error()
    #define TRACE(err) (err += BacktraceEntry{__FILE__, __FUNCTION__, __LINE__})

    struct BacktraceEntry
    {
        std::string_view file;
        std::string_view function;
        int32_t line;
    };

    class Error
    {
    public:
        Error()  = default;
        ~Error() = default;

        Error(Error&& other) = default;
        Error& operator=(Error&& other) = default;

        Error(Error const& other) = default;
        Error& operator=(Error const& other) = default;

        Error(int32_t code, std::string_view what);
        Error& operator+=(BacktraceEntry const& here);

        operator bool() const         { return (code_ == 0); }
        int32_t code() const          { return code_; }
        std::string_view what() const { return what_; }

        void print() const;

    private:
        int32_t code_{0};
        std::string_view what_{"Success"};

        int32_t backtrace_index_{0};
        std::array<BacktraceEntry, 16> backtrace_;
    };

    bool operator==(Error const& lhs, Error const& rhs);

    Error const E_INVALID_WKC           { 0x0002, "Invalid working counter"                 };
    Error const E_TRANSITION            { 0x0003, "Invalid transition to requested state"   };
    Error const E_TIMEOUT               { 0x0004, "Operation timed out"                     };
    Error const E_INVALID_FRAME_TYPE    { 0x0005, "Invalid frame type"                      };
    Error const E_READ_ERROR            { 0x0006, "Read error"                              };
    Error const E_WRITE_ERROR           { 0x0007, "Write error"                             };
    Error const E_WOULD_BLOCK           { 0x0008, "Operation would block"                   };
}

#endif
