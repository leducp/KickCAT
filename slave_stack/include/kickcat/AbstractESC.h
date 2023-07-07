#ifndef SLAVE_STACK_INCLUDE_ABSTRACTESC_H_
#define SLAVE_STACK_INCLUDE_ABSTRACTESC_H_

#include <cstdint>
#include "Error.h"


namespace kickcat
{
    class AbstractESC
    {
    public:
        virtual ~AbstractESC() = default;

        virtual hresult init() = 0;

        virtual hresult read(uint16_t address, void* data, uint16_t size) = 0;
        virtual hresult write(uint16_t address, void const* data, uint16_t size) = 0;
    };

}
#endif
