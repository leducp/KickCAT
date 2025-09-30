#include "kickcat/nuttx/SPI.h"
#include "kickcat/Error.h"
#include "kickcat/debug.h"
#include <fcntl.h>
#include <cstdio>

namespace kickcat
{
    SPI::SPI()
        : fd_{-1}
        , priv_spi_{nullptr}
    {

    }

    SPI::~SPI()
    {
        close();
    }

    void SPI::close()
    {
        if (priv_spi_)
        {
            SPI_SELECT(*priv_spi_, chipSelect_, false);
            SPI_LOCK(*priv_spi_, false);
            priv_spi_ = nullptr;
        }
        if (fd_ >= 0)
        {
            file_close(&filep_spi_);
            fd_ = -1;
        }
    }

    void SPI::open(std::string const& device, uint8_t CPOL, uint8_t CPHA, uint32_t baudrate)
    {
        fd_ = file_open(&filep_spi_, device.c_str(), O_RDWR);
        if (fd_ < 0)
        {
            THROW_SYSTEM_ERROR_CODE("file_open()", -fd_);
        }
        spi_info("Opened %s\n", device.c_str());
        device_ = device;

        // Take ownership of the SPI (no support for shared access)
        priv_spi_ = static_cast<spi_dev_s**>(filep_spi_.f_inode->i_private);
        SPI_LOCK(*priv_spi_, true);

        setMode(CPOL, CPHA);
        setBaudRate(baudrate);
        spi_info("Configuring SPI: baud=%lu, CPOL=%u, CPHA=%u\n", baudrate_, CPOL_, CPHA_);
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

    void SPI::setMode(uint8_t CPOL, uint8_t CPHA)
    {
        if      ((CPOL == 0) and (CPHA == 0)) { SPI_SETMODE(*priv_spi_, SPIDEV_MODE0); return; }
        else if ((CPOL == 0) and (CPHA == 1)) { SPI_SETMODE(*priv_spi_, SPIDEV_MODE1); return; }
        else if ((CPOL == 1) and (CPHA == 0)) { SPI_SETMODE(*priv_spi_, SPIDEV_MODE2); return; }
        else if ((CPOL == 1) and (CPHA == 1)) { SPI_SETMODE(*priv_spi_, SPIDEV_MODE3); return; }
        else
        {
            THROW_SYSTEM_ERROR_CODE("setMode()", EINVAL);
        }

        CPOL_ = CPOL;
        CPHA_ = CPHA;
    }

    void SPI::setBaudRate(uint32_t baudrate)
    {
        SPI_SETFREQUENCY(*priv_spi_, baudrate);
        baudrate_ = baudrate;
    }
}
