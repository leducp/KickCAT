#include "SPI.h"

#include <fcntl.h>

namespace kickcat
{
    void SPI::init()
    {
        fd_ = file_open(&filep_spi_, "/dev/spi0", O_RDWR);
        printf("Open spi0 driver file fd: %d \n", fd_);

        priv_spi_ = static_cast<spi_dev_s**>(filep_spi_.f_inode->i_private);

        printf("Setting SPI Frequecy and mode \n");
        // Setup SPI frequency and mode
        SPI_SETFREQUENCY(*priv_spi_, 4000000);
        SPI_SETMODE(*priv_spi_, SPIDEV_MODE0);

        SPI_LOCK(*priv_spi_, true);
    }


    void SPI::transfer(uint8_t const* data_write, uint8_t* data_read, uint32_t size)
    {
        SPI_EXCHANGE(*priv_spi_, data_write, data_read, size);
    }

    SPI::~SPI()
    {
        SPI_SELECT(*priv_spi_, 0, false);
        SPI_LOCK(*priv_spi_, false);
        file_close(&filep_spi_);
    }
}
