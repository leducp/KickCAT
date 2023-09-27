
#ifndef SLAVE_STACK_INCLUDE_ABSTRACTSPI_H_
#define SLAVE_STACK_INCLUDE_ABSTRACTSPI_H_

#include <nuttx/config.h>
#include <nuttx/spi/spi.h>
#include <nuttx/spi/spi_transfer.h>
#include <sys/ioctl.h>

#include <stdlib.h>

#include <stdio.h>
#include <fcntl.h>

namespace kickcat
{
    class SPI
    {
    public:
        virtual ~SPI() = default;

        virtual void init();
        void read( void* data, uint32_t size);
        void write(void const* data, uint32_t size);
        virtual void transfer(uint8_t const* data_write, uint8_t* data_read, uint32_t size);

        // Done internally by ioctl transfer
        virtual void enableChipSelect(){};
        virtual void disableChipSelect(){};

    private:
        struct spi_sequence_s seq_;
        struct spi_trans_s trans_;
        int fd_;
    };

}
#endif
