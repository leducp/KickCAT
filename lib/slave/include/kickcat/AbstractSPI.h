#ifndef SLAVE_STACK_INCLUDE_ABSTRACTSPI_H_
#define SLAVE_STACK_INCLUDE_ABSTRACTSPI_H_

#include <cstdint>
#include <string>
#include <tuple>

namespace kickcat
{
    class AbstractSPI
    {
    public:
        AbstractSPI() = default;
        virtual ~AbstractSPI() = default;

        virtual void open(std::string const& device, uint8_t CPOL, uint8_t CPHA, uint32_t baudrate) = 0;
        virtual void close() = 0;

        void read(void* data, uint32_t size);
        void write(void const* data, uint32_t size);
        virtual void transfer(uint8_t const* data_write, uint8_t* data_read, uint32_t size) = 0;

        virtual void enableChipSelect() = 0;
        virtual void disableChipSelect() = 0;

        virtual void setBaudRate(uint32_t baudrate) = 0;
        uint32_t baudRate() const { return baudrate_; }

        virtual void setMode(uint8_t CPOL, uint8_t CPHA) = 0;
        std::tuple<uint8_t, uint8_t> mode() const { return {CPOL_, CPHA_}; }

        std::string device() const { return device_; }

        void setChipSelect(uint32_t cs) { chipSelect_ = cs; }
        uint32_t chipSelect() const { return chipSelect_; }

    protected:
        std::string device_{};    // device opened - implementation specific
        uint32_t baudrate_{};     // baudrate in Hz
        uint32_t chipSelect_{};   // id of current chip select - implementation specific
        uint8_t CPOL_{};
        uint8_t CPHA_{};
    };
}

#endif
