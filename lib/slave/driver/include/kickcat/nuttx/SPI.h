#ifndef SLAVE_STACK_INCLUDE_NUTTX_SPI_H_
#define SLAVE_STACK_INCLUDE_NUTTX_SPI_H_

#include "kickcat/AbstractSPI.h"
#include <nuttx/spi/spi_transfer.h>
#include <string>

namespace kickcat
{
    class SPI final : public AbstractSPI
    {
    public:
        SPI();
        virtual ~SPI();

        void open(std::string const& device, uint8_t CPOL, uint8_t CPHA, uint32_t baudrate) override;
        void close() override;

        void transfer(uint8_t const* data_write, uint8_t* data_read, uint32_t size) override;

        void enableChipSelect() override;
        void disableChipSelect() override;

        void setMode(uint8_t CPOL, uint8_t CPHA) override;
        void setBaudRate(uint32_t baud) override;

    private:
        file filep_spi_;
        int fd_;
        spi_dev_s** priv_spi_;
    };
}

#endif
