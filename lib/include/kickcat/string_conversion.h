#ifndef KICKCAT_STRING_CONVERSION_H
#define KICKCAT_STRING_CONVERSION_H

#include <cstdio>
#include <string>
#include <type_traits>

namespace kickcat
{
    template<typename T>
    std::string toDec(T value)
    {
        char buf[24];
        if constexpr (std::is_signed_v<T>)
        {
            std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(value));
        }
        else
        {
            std::snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(value));
        }
        return std::string(buf);
    }

    inline std::string toHex(unsigned int value)
    {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%x", value);
        return std::string(buf);
    }
}

#endif
