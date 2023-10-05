#ifndef SLAVE_STACK_INCLUDE_SLAVE_H_
#define SLAVE_STACK_INCLUDE_SLAVE_H_

#include <cstdint>
#include "kickcat/Error.h"


namespace kickcat
{
    class Slave
    {
    public:
        virtual ~Slave() = default;

        virtual hresult init() = 0;

        hresult readPDO(void* data, uint16_t size);
        hresult writePDO(void const* data, uint16_t size);
    };

}
#endif
