#ifndef KICKCAT_ERROR_H
#define KICKCAT_ERROR_H

#include <cstdint>
#include <string>
#include <vector>

namespace kickcat
{
    #define ESUCCESS Error()
    #define EERROR(err) Error(err, __FUNCTION__, __FILE__, __LINE__)

    class Error
    {
    public:
        Error()  = default;
        ~Error() = default;

        Error(Error&& other) = default;
        Error& operator=(Error&& other) = default;

        Error(Error const& other) = delete;
        Error& operator=(Error const& other) = delete;

        Error(std::string const& message,
                  std::string const& function,
                  std::string const& file,
                  int32_t line);

        Error& operator+=(Error&& other);

        operator bool() const { return isError_; }
        void what() const;

    private:
        bool isError_{false};
        std::string what_{"success"};
        std::vector<std::string> backtrace_;
    };
}

#endif
