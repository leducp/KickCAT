#include "kickcat/ESC/XMC4800.h"
#include "cstring"

namespace kickcat
{

    hresult XMC4800::init()
    {
        return hresult::OK;
    }


    hresult XMC4800::read(uint16_t address, void* data, uint16_t size)
    {
        if (ECAT0_BASE_ADDRESS + address + size > ECAT0_END_ADDRESS)
        {
            return hresult::E_ERANGE;
        }

        std::memcpy(data, reinterpret_cast<void*>(ECAT0_BASE_ADDRESS + address), size);
        return hresult::OK;
    }


    hresult XMC4800::write(uint16_t address, void const* data, uint16_t size)
    {
        if (ECAT0_BASE_ADDRESS + address + size > ECAT0_END_ADDRESS)
        {
            return hresult::E_ERANGE;
        }

        std::memcpy(reinterpret_cast<void*>(ECAT0_BASE_ADDRESS + address), data, size);
        return hresult::OK;
    }
}
