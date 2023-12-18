
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
        void read( void* data, uint32_t size);
        void write(void const* data, uint32_t size);
        virtual void transfer(uint8_t const* data_write, uint8_t* data_read, uint32_t size) = 0;

        virtual void enableChipSelect() = 0;
        virtual void disableChipSelect() = 0;
    };

}
#endif
