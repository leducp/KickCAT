#include "checksum/adler32.h"

namespace kickcat
{
    uint32_t adler32Sum(void const* buffer, std::size_t size)
    {
        uint8_t const* ptr = reinterpret_cast<uint8_t const*>(buffer);
        uint32_t a = 1;
        uint32_t b = 0;
        for (std::size_t i = 0; i < size; ++i)
        {
            a = (a + ptr[i]) % MOD_ADLER;
            b = (b + a) % MOD_ADLER;
        }
        return (b << 16) | a;
    }
}
