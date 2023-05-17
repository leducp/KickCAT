
#ifndef SLAVE_STACK_INCLUDE_ABSTRACTSPI_H_
#define SLAVE_STACK_INCLUDE_ABSTRACTSPI_H_

#include <cstdint>

namespace kickcat
{
    class AbstractSPI
    {
    public:
        virtual ~AbstractSPI() = default;

        virtual void init() = 0;
        virtual uint8_t read() = 0;
        virtual void write(uint8_t data) = 0;
    };

}
#endif
