
#ifndef SLAVE_STACK_INCLUDE_NUTTX_SPI_H_
#define SLAVE_STACK_INCLUDE_NUTTX_SPI_H_

#include "kickcat/AbstractSPI.h"
#include <nuttx/spi/spi_transfer.h>

#include <stdio.h>

namespace kickcat
{
    class SPI : public AbstractSPI
    {
    public:
        SPI(int spi_number);
        ~SPI();

        void init() override;
        void transfer(uint8_t const* data_write, uint8_t* data_read, uint32_t size) override;

        void enableChipSelect() override {SPI_SELECT(*priv_spi_, 0, true);};
        void disableChipSelect() override {SPI_SELECT(*priv_spi_, 0, false);};

    private:
        file filep_spi_;
        int fd_;
        spi_dev_s** priv_spi_;
        int SPI_number;
    };
}
#endif
