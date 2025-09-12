#include "kickcat/nuttx/SPI.h"
#include <fcntl.h>
#include <cstdio>

namespace kickcat
{
    SPI::SPI()
        : fd_(-1)
        , priv_spi_(nullptr)
        , devicePath_("/dev/spi0")
        , baudRate_(10000000) // default 10 MHz
        , mode_(SPIDEV_MODE0)
        , chipSelect_(0)
    {}

    void SPI::init()
    {
        fd_ = file_open(&filep_spi_, devicePath_.c_str(), O_RDWR);
        if (fd_ < 0)
        {
            printf("Failed to open %s\n", devicePath_.c_str());
            return;
        }
        printf("Opened %s with fd: %d\n", devicePath_.c_str(), fd_);

        priv_spi_ = static_cast<spi_dev_s**>(filep_spi_.f_inode->i_private);

        printf("Configuring SPI: baud=%lu, mode=%u, cs=%lu\n", baudRate_, mode_, chipSelect_);
        SPI_SETFREQUENCY(*priv_spi_, baudRate_);
        SPI_SETMODE(*priv_spi_, static_cast<spi_mode_e>(mode_));
        SPI_LOCK(*priv_spi_, true);
    }

    void SPI::transfer(uint8_t const* data_write, uint8_t* data_read, uint32_t size)
    {
        SPI_EXCHANGE(*priv_spi_, data_write, data_read, size);
    }

    void SPI::enableChipSelect()
    {
        SPI_SELECT(*priv_spi_, chipSelect_, true);
    }

    void SPI::disableChipSelect()
    {
        SPI_SELECT(*priv_spi_, chipSelect_, false);
    }

    SPI::~SPI()
    {
        if (priv_spi_)
        {
            SPI_SELECT(*priv_spi_, chipSelect_, false);
            SPI_LOCK(*priv_spi_, false);
        }
        if (fd_ >= 0)
        {
            file_close(&filep_spi_);
        }
    }
}
