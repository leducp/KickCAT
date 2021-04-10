#include "Error.h"

#include <iostream>

namespace kickcat
{
    Error::Error(std::string const& message,
                 std::string const& function,
                 std::string const& file,
                 int32_t line)
        : isError_{true}
        , what_{function + ": " + message + " (" + file + ":" + std::to_string(line) + ")"}
    { }


    void Error::what() const
    {
        std::cout << what_ << std::endl;
        for (auto b : backtrace_)
        {
            std::cout << " from: " << b << std::endl;
        }
    }


    Error& Error::operator+=(Error&& other)
    {
        backtrace_.emplace_back(std::move(other.what_));
        return *this;
    }
}
