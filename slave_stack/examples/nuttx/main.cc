
#include "nuttx/SPI.h"

#include <string.h>

#include <unistd.h>

extern "C" int main(int argc, char* argv[])
{
    printf("HELLO \n");

    kickcat::SPI spi;

    uint8_t txdata[40] = {0};
    uint8_t rxdata[40] = {0};
    uint32_t data = 0x640003;
    memcpy(&txdata[0], &data, 3);
    spi.init();

    while(true)
    {
        spi.transfer(txdata, rxdata, 7);
        usleep(500000);
    }

    return 0;
}



//#include <nuttx/config.h>
//#include <nuttx/spi/spi.h>
//#include <nuttx/spi/spi_transfer.h>
//#include <sys/ioctl.h>
//
//#include <stdlib.h>
//
//#include <stdio.h>
//#include <fcntl.h>
//#include <unistd.h>
//
//void my_awesome_test()
//{
//    printf("It's a brave new world ! \n");
//}


//class NuttxSPI
//{
//public:
//    virtual ~NuttxSPI()
//    {
//        close(fd_);
//    }
//
//    virtual void init()
//    {
//        fd_ = ::open("/dev/spi0", O_RDONLY);
//
//        if (fd_ < 0)
//        {
//            printf("Failed to get bus %d, errno: %s \n", fd_, strerror(errno));
//        }
//
//        seq_.dev = SPIDEV_ID(SPIDEVTYPE_ETHERNET, 0);
//        seq_.mode = 0;
//        seq_.nbits = 8;
//        seq_.frequency = 4000000;
//        seq_.ntrans = 1;
//        seq_.trans = &trans_;
//
//        trans_.deselect = true;
//        trans_.delay = 0;
//    }
//
//
//    void read( void* data, uint32_t size){};
//    void write(void const* data, uint32_t size){};
//    virtual void transfer(uint8_t const* data_write, uint8_t* data_read, uint32_t size)
//    {
//        trans_.txbuffer = data_write;
//        trans_.rxbuffer = data_read;
//        trans_.nwords = size;
//        ioctl(fd_, SPIIOC_TRANSFER, (unsigned long)((uintptr_t)&seq_));
//    }
//
//    // Done internally by ioctl transfer
//    virtual void enableChipSelect(){};
//    virtual void disableChipSelect(){};
//
//private:
//    struct spi_sequence_s seq_;
//    struct spi_trans_s trans_;
//    int fd_;

//    uint8_t txdata_[40] = {0};
//    uint8_t rxdata_[40] = {0};
//};
//
//extern "C" int main(int argc, char *argv[])
//{
//    my_awesome_test();
//
//
//    NuttxSPI spi;
//
//    uint8_t txdata[40] = {0};
//    uint8_t rxdata[40] = {0};
//    uint32_t data = 0x640003;
//    memcpy(&txdata[0], &data, 3);
//    spi.init();
//
//    while(true)
//    {
//        printf("Transfer ! \n");
//        spi.transfer(txdata, rxdata, 7);
//        usleep(1000000);
//    }
//
//    return 0;
//}




