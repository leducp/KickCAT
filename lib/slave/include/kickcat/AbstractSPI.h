#ifndef SLAVE_STACK_INCLUDE_ABSTRACTSPI_H_
#define SLAVE_STACK_INCLUDE_ABSTRACTSPI_H_

#include <cstdint>
#include <string>

namespace kickcat
{
    class AbstractSPI
    {
    public:
        virtual ~AbstractSPI() = default;

        virtual void init() = 0;

        void read(void* data, uint32_t size);
        void write(void const* data, uint32_t size);
        virtual void transfer(uint8_t const* data_write, uint8_t* data_read, uint32_t size) = 0;

        virtual void enableChipSelect() = 0;
        virtual void disableChipSelect() = 0;

        virtual void setBaudRate(uint32_t baud) = 0;
        virtual uint32_t getBaudRate() const = 0;

        virtual void setMode(uint8_t mode) = 0;
        virtual uint8_t getMode() const = 0;

        virtual void setDevice(const std::string& devicePath) = 0;
        virtual std::string getDevice() const = 0;

        virtual void setChipSelect(uint32_t cs) = 0;
        virtual uint32_t getChipSelect() const = 0;
    };
}

#endif
