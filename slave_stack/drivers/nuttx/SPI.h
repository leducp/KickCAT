
#ifndef SLAVE_STACK_INCLUDE_NUTTX_SPI_H_
#define SLAVE_STACK_INCLUDE_NUTTX_SPI_H_

#include "kickcat/AbstractSPI.h"
#include <nuttx/spi/spi_transfer.h>

#include <stdlib.h>

#include <stdio.h>
#include <dirent.h>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>



namespace kickcat
{
    struct spi_driver_s
    {
        FAR struct spi_dev_s* spi; /* Contained SPI lower half driver */
    #ifndef CONFIG_DISABLE_PSEUDOFS_OPERATIONS
        mutex_t lock;              /* Mutual exclusion */
        int16_t crefs;             /* Number of open references */
        bool unlinked;             /* True, driver has been unlinked */
    #endif
    };


    class SPI : public AbstractSPI
    {
    public:
        ~SPI();

        void init() override;
        void transfer(uint8_t const* data_write, uint8_t* data_read, uint32_t size) override;

        // Done internally by ioctl transfer
        void enableChipSelect() override {SPI_SELECT(priv_spi_->spi, 0, true);};
        void disableChipSelect() override {SPI_SELECT(priv_spi_->spi, 0, false);};

    private:
//        struct spi_sequence_s seq_;
//        struct spi_trans_s trans_;


        file filep_spi_;
        int fd_;
        spi_driver_s* priv_spi_;
    };

}
#endif
