#include "kickcat/ESC/XMC4800.h"
#include "cstring"

namespace kickcat
{
// LCOV_EXCL_START
    int32_t XMC4800::read(uint16_t address, void* data, uint16_t size)
    {
        if (ECAT0_BASE_ADDRESS + address + size > ECAT0_END_ADDRESS)
        {
            return -1;
            //return hresult::E_ERANGE;
        }

        std::memcpy(data, reinterpret_cast<void*>(ECAT0_BASE_ADDRESS + address), size);
        return size;
    }


    int32_t XMC4800::write(uint16_t address, void const* data, uint16_t size)
    {
        if (ECAT0_BASE_ADDRESS + address + size > ECAT0_END_ADDRESS)
        {
            return -1;
            //return hresult::E_ERANGE;
        }

        std::memcpy(reinterpret_cast<void*>(ECAT0_BASE_ADDRESS + address), data, size);
        return size;
    }
// LCOV_EXCL_STOP
}
