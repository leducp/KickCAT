#include "kickcat/AbstractSPI.h"
#include <vector>


namespace kickcat
{

    void AbstractSPI::read(void* data, uint32_t size)
    {
        transfer(nullptr, reinterpret_cast<uint8_t *>(data), size);
    }


    void AbstractSPI::write(void const* data, uint32_t size)
    {
        transfer(reinterpret_cast<uint8_t const*>(data), nullptr, size);
    }
}
