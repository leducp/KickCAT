#include "SPI.h"

//#include <sys/ioctl.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>


namespace kickcat
{
    void SPI::init()
    {
        fd_ = file_open(&filep_spi_, "/dev/spi0", O_RDWR);
        printf("Open spi0 driver file fd: %d \n", fd_);

        priv_spi_ = static_cast<spi_driver_s*>(filep_spi_.f_inode->i_private);

        printf("Setting SPI Frequecy and mode \n");
        // Setup SPI frequency and mode
        SPI_SETFREQUENCY(priv_spi_->spi, 4000000);
        SPI_SETMODE(priv_spi_->spi, SPIDEV_MODE0);

        SPI_LOCK(priv_spi_->spi, true);
    }


    void SPI::transfer(uint8_t const* data_write, uint8_t* data_read, uint32_t size)
    {
        SPI_EXCHANGE(priv_spi_->spi, data_write, data_read, size);
    }

    SPI::~SPI()
    {
        SPI_SELECT(priv_spi_->spi, 0, false);
        SPI_LOCK(priv_spi_->spi, false);
        file_close(&filep_spi_);
    }


//    void SPI::init()
//    {
//        fd_ = open("/dev/spi0", O_RDONLY);
//
//        if (fd_ < 0)
//        {
//            printf("Failed to get bus %d\n", fd_);
//        }
//
//        seq_.dev = SPIDEV_ID(SPIDEVTYPE_ETHERNET, 0);
//        seq_.mode = 0;
//        seq_.nbits = 8;
//        seq_.frequency = 4000000;
//        seq_.ntrans = 1;
//        seq_.trans = &trans_;
//
//        trans_.deselect = false;
//        trans_.delay = 0;
//    }
//
//
//    void SPI::transfer(uint8_t const* data_write, uint8_t* data_read, uint32_t size)
//    {
//        printf("TRANSFER \n");
//        trans_.txbuffer = data_write;
//        trans_.rxbuffer = data_read;
//        trans_.nwords = size;
//        ioctl(fd_, SPIIOC_TRANSFER, (unsigned long)((uintptr_t)&seq_));
//    }
}
