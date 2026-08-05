#ifndef KICKCAT_UTILS_MATH_H
#define KICKCAT_UTILS_MATH_H

#include <cstdint>
#include <limits>
#include <type_traits>

namespace kickcat
{
    // Freestanding min/max/abs/round/convert helpers that pull no <algorithm>/<cmath>/<cstdlib>.
    // In a subdirectory on purpose: lib/include/kickcat is itself on the include path, so a math.h
    // sitting directly in it is what every translation unit here would get for #include <math.h>.
    // Those standard headers are broken or mutually conflicting on some embedded C++ exports (e.g.
    // NuttX on arm-none-eabi, where cxx/cmath references an absent ::nextafterl and <cstdlib>
    // collides with the toolchain's stdlib.h on div_t) so code compiled for those targets uses these
    // instead. <limits> is required of a freestanding implementation, so it is safe here.

    template<typename T>
    constexpr T clamp(T v, T lo, T hi)
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

    // Only NaN compares unequal to itself, and <cmath> is one of the headers this file avoids.
    constexpr bool is_nan(double v)
    {
        return v != v;
    }

    template<typename T>
    constexpr T abs_value(T v)
    {
        if (v < 0)
        {
            return -v;
        }
        return v;
    }

    /// \brief Convert a real value to T, clamping it into [min, max] reduced into T's range.
    /// \details static_cast<T>(double) is undefined behaviour outside T's range, for a narrower
    ///          floating type as much as for an integer. Bounds the destination cannot hold give a
    ///          meaningless result, never undefined behaviour.
    template<typename T>
    constexpr T saturate(double v, double min, double max)
    {
        static_assert(std::is_arithmetic_v<T>, "saturate() converts to an arithmetic type");

        // lowest(), not min(): for a floating T the latter is the smallest positive normal.
        // Beyond 32 bits these images are not exact - numeric_limits<int64_t>::max() rounds up to
        // 2^63 - so they are thresholds to compare against, never values to convert back.
        double const lowest  = static_cast<double>(std::numeric_limits<T>::lowest());
        double const highest = static_cast<double>(std::numeric_limits<T>::max());
        double const first   = clamp(min, lowest, highest);
        double const second  = clamp(max, lowest, highest);

        // Transposed bounds still describe the interval between them; rejecting them belongs to the
        // caller-facing API (see Drive::setLimits).
        double lo = first;
        double hi = second;
        if (second < first)
        {
            lo = second;
            hi = first;
        }

        if (v >= hi)
        {
            if (hi >= highest)
            {
                return std::numeric_limits<T>::max();
            }
            return static_cast<T>(hi);
        }

        if (v <= lo)
        {
            if (lo <= lowest)
            {
                return std::numeric_limits<T>::lowest();
            }
            return static_cast<T>(lo);
        }

        return static_cast<T>(v);
    }

    /// \brief Round to the nearest integer, saturating: the destination range is the only bound a
    ///        bare double to int64_t conversion has, and exceeding it would be undefined.
    constexpr int64_t round_to_int(double v)
    {
        constexpr double lowest  = static_cast<double>(std::numeric_limits<int64_t>::lowest());
        constexpr double highest = static_cast<double>(std::numeric_limits<int64_t>::max());

        if (v < 0.0)
        {
            return saturate<int64_t>(v - 0.5, lowest, highest);
        }
        return saturate<int64_t>(v + 0.5, lowest, highest);
    }
}

#endif
