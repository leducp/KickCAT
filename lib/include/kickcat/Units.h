#ifndef KICKCAT_UNITS_H
#define KICKCAT_UNITS_H

#include <cstddef>
#include <cstdint>

namespace kickcat
{
    constexpr std::size_t operator""_KiB(unsigned long long const x) { return static_cast<std::size_t>(1024u    * x); }
    constexpr std::size_t operator""_MiB(unsigned long long const x) { return static_cast<std::size_t>(1024_KiB * x); }
    constexpr std::size_t operator""_GiB(unsigned long long const x) { return static_cast<std::size_t>(1024_MiB * x); }
    constexpr std::size_t operator""_TiB(unsigned long long const x) { return static_cast<std::size_t>(1024_GiB * x); }
}

#endif
