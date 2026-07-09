#ifndef KICKCAT_OS_MATH_H
#define KICKCAT_OS_MATH_H

#include <cstdint>

namespace kickcat
{
    // Freestanding min/max/abs/round helpers that pull no <algorithm>/<cmath>/<cstdlib>. Those
    // standard headers are broken or mutually conflicting on some embedded C++ exports (e.g. NuttX
    // on arm-none-eabi, where cxx/cmath references an absent ::nextafterl and <cstdlib> collides with
    // the toolchain's stdlib.h on div_t) so code compiled for those targets uses these instead.
    // Prefer the std equivalents (std::clamp / std::abs / std::llround) in any translation unit that
    // already compiles with those headers.

    template<typename T>
    T clamp(T v, T lo, T hi)
    {
        if (v < lo)
        {
            return lo;
        }
        if (v > hi)
        {
            return hi;
        }
        return v;
    }

    template<typename T>
    T abs_value(T v)
    {
        if (v < 0)
        {
            return -v;
        }
        return v;
    }

    inline int64_t round_to_int(double v)
    {
        if (v < 0.0)
        {
            return static_cast<int64_t>(v - 0.5);
        }
        return static_cast<int64_t>(v + 0.5);
    }
}

#endif
