#ifndef SLAVE_STACK_INCLUDE_ABSTRACTESC_H_
#define SLAVE_STACK_INCLUDE_ABSTRACTESC_H_

#include <cstdint>

namespace kickcat
{
    class AbstractESC
    {
    public:
        virtual ~AbstractESC() = default;

        virtual void init() = 0;

        virtual int32_t readRegister(uint16_t address, void* data, uint32_t size) = 0;
        virtual int32_t writeRegister(uint16_t address, void const* data, uint32_t size) = 0;


        // TODO move to slave abstraction
//        virtual void readPDO(uint8_t* data, uint32_t size) = 0;
//        virtual void writePDO(uint8_t* data, uint32_t size) = 0;
//
//        virtual int32_t readEEPROM(uint8_t* data, uint32_t size) = 0;
//        virtual int32_t writeEEPROM(uint8_t* data, uint32_t size) = 0;

        // TODO mailbox
    };

}
#endif
