#include "SPI.h"


namespace kickcat
{
    void SPI::init()
    {
        fd_ = open("/dev/spi0", O_RDONLY);

        if (fd_ < 0)
        {
            printf("Failed to get bus %d\n", fd_);
        }

        seq_.dev = SPIDEV_ID(SPIDEVTYPE_ETHERNET, 0);
        seq_.mode = 0;
        seq_.nbits = 8;
        seq_.frequency = 4000000;
        seq_.ntrans = 1;
        seq_.trans = &trans_;

        trans_.deselect = true;
        trans_.delay = 0;
    }


    void SPI::transfer(uint8_t const* data_write, uint8_t* data_read, uint32_t size)
    {
        printf("TRANSFER \n");
        trans_.txbuffer = data_write;
        trans_.rxbuffer = data_read;
        trans_.nwords = size;
        ioctl(fd_, SPIIOC_TRANSFER, (unsigned long)((uintptr_t)&seq_));
    }
}
