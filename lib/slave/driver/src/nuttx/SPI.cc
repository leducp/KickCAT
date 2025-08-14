#include "kickcat/nuttx/SPI.h"

#include <fcntl.h>
#include <string>

namespace kickcat
{
    SPI:: SPI(int spi_number): SPI_number_{spi_number}
    {
    }

    void SPI::init()
    {
        std::string path = "/dev/spi" + std::to_string(SPI_number_);
        fd_ = file_open(&filep_spi_, path.c_str(), O_RDWR);
        printf("Open spi%d driver file fd: %d \n", SPI_number_, fd_);

        priv_spi_ = static_cast<spi_dev_s**>(filep_spi_.f_inode->i_private);

        printf("Setting SPI Frequecy and mode \n");
        // Setup SPI frequency and mode
        SPI_SETFREQUENCY(*priv_spi_, 10000000); // set 4 000 000 Hz to use oscillo measurements.
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
