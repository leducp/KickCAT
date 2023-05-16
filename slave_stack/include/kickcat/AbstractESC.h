#ifndef SLAVE_STACK_INCLUDE_ABSTRACTESC_H_
#define SLAVE_STACK_INCLUDE_ABSTRACTESC_H_

#include <cstdint>

namespace kickcat
{
    class AbstractESC
    {
        virtual ~AbstractESC();

        virtual uint32_t readRegister(uint16_t address, uint8_t len) = 0;
        virtual
    };

}
#endif
