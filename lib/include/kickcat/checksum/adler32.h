#ifndef KICKCAT_ADDLER32_SUM
#define KICKCAT_ADDLER32_SUM

#include <vector>
#include <cstdint>


namespace kickcat
{
    constexpr uint32_t MOD_ADLER = 65521;
    uint32_t adler32Sum(void const* buffer, std::size_t size);
}

#endif
