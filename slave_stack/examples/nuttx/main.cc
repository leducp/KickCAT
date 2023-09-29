#include <nuttx/config.h>
#include <nuttx/spi/spi.h>
#include <nuttx/spi/spi_transfer.h>
#include <sys/ioctl.h>

#include <stdlib.h>

#include <stdio.h>
#include <dirent.h>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#include <sys/boardctl.h>

#include "nuttx/SPI.h"
#include "kickcat/ESC/Lan9252.h"

using namespace kickcat;

void reportError(hresult const& rc)
{
    if (rc != hresult::OK)
    {
        printf("%s\n", toString(rc));
    }
}


//    struct spi_driver_s
//    {
//        FAR struct spi_dev_s* spi; /* Contained SPI lower half driver */
//#ifndef CONFIG_DISABLE_PSEUDOFS_OPERATIONS
//        mutex_t lock;              /* Mutual exclusion */
//        int16_t crefs;             /* Number of open references */
//        bool unlinked;             /* True, driver has been unlinked */
//#endif
//    };

void esc_routine(Lan9252& esc)
{
    uint32_t nb_bytes = 32;
    uint8_t test_write[nb_bytes];
    for (uint32_t i=0; i < nb_bytes; ++i)
    {
        test_write[i] = i;
    }
    reportError(esc.write(0x1200, &test_write, nb_bytes));

    uint16_t al_status;
    reportError(esc.read(AL_STATUS, &al_status, sizeof(al_status)));
    bool watchdog = false;
    reportError(esc.read(WDOG_STATUS, &watchdog, 1));

    if ((al_status & ESM_OP) and watchdog)
    {
        uint8_t test_read[nb_bytes];
        reportError(esc.read(0x1000, &test_read, nb_bytes));
        for (uint32_t i=0; i < nb_bytes; i++)
        {
            printf("%x ", test_read[i]);
        }
        printf(" received\n");
    }
}

extern "C" int main(int argc, char *argv[])
{

//    file filepSpi;
//    int fd;
//    struct spi_dev_s* privSpi_;
//
//
//    fd = file_open(&filepSpi, "/dev/spi0", O_RDWR);
//    printf("Open spi0 driver file fd: %d \n", fd);
//
//    inode* inodeSpi = filepSpi.f_inode;
//    privSpi_        = static_cast<spi_dev_s*>(inodeSpi->i_private);
//
//    printf("Setting SPI Frequecy and mode \n");
//    // Setup SPI frequency and mode
//    SPI_SETFREQUENCY(privSpi_, 4000000);
//    SPI_SETMODE(privSpi_, SPIDEV_MODE0);
//
//    // Lock the SPI bus so that only one device can access it at the same time
//    SPI_LOCK(privSpi_, true);
//
//    // Check WHO AM I register
//    uint8_t tx[8] = {0x03, 0x00, 0x64, 0x00};
//    uint8_t rx[8] = {};
//    SPI_SELECT(privSpi_, 0, true);
//    SPI_EXCHANGE(privSpi_, tx, rx, 7);
//    SPI_SELECT(privSpi_, 0, false);
//
//    printf("WHO AM I: %x \n", rx[1]);



    ///////////////////////////////////////////////////////
    // boardctl(BOARDIOC_INIT, 0);

//    file filepSpi;
//    int fd;
//    spi_driver_s* privSpi_;
//
//
//    fd = file_open(&filepSpi, "/dev/spi0", O_RDWR);
//    printf("Open spi0 driver file fd: %d \n", fd);
//
//    inode* inodeSpi = filepSpi.f_inode;
//    privSpi_        = static_cast<spi_driver_s*>(filepSpi.f_inode->i_private);
//
//    printf("Setting SPI Frequecy and mode \n");
//    // Setup SPI frequency and mode
//    SPI_SETFREQUENCY(privSpi_->spi, 4000000);
//    SPI_SETMODE(privSpi_->spi, SPIDEV_MODE0);
//
//    // Lock the SPI bus so that only one device can access it at the same time
//    SPI_LOCK(privSpi_->spi, true);
//
//    // Check WHO AM I register
//    uint8_t tx[8] = {0x03, 0x00, 0x64, 0x00};
//    uint8_t rx[8] = {};
//    SPI_SELECT(privSpi_->spi, 0, true);
//    SPI_EXCHANGE(privSpi_->spi, tx, rx, 7);
//    SPI_SELECT(privSpi_->spi, 0, false);
//
//    printf("WHO AM I: %x \n", rx[1]);


////////////////////////////////////////////////////////////////////////////////////////////////////////////


//    printf("HELLO \n");
//
//   uint8_t txdata[40] = {0};
//   uint8_t rxdata[40] = {0};
//   uint32_t data = 0x640003;
//   memset(txdata, 0, 40);
//   // memcpy(txdata, &data, 4);
//
//   txdata[0] = 0x03;
//   txdata[1] = 0x00;
//   txdata[2] = 0x64;
//
//
//   sleep(1);
//   printf("bonjour: %x %x %x %x\n",
//   txdata[0], txdata[1], txdata[2], txdata[3]);
//
//
//   struct spi_sequence_s seq_;
//   struct spi_trans_s trans_;
//   int fd_ = open("/dev/spi0", O_RDONLY);
//
//   if (fd_ < 0)
//   {
//       printf("Failed to get bus %d\n", fd_);
//   }
//
//   seq_.dev = SPIDEV_ID(SPIDEVTYPE_ETHERNET, 0);
//   seq_.mode = 0;
//   seq_.nbits = 8;
//   seq_.frequency = 4000000;
//   seq_.ntrans = 1;
//   seq_.trans = &trans_;
//
//   trans_.deselect = true;
//   trans_.delay = 0;
//
//   trans_.txbuffer = txdata;
//   trans_.rxbuffer = rxdata;
//   trans_.nwords = 7;
//   ioctl(fd_, SPIIOC_TRANSFER, (unsigned long)((uintptr_t)&seq_));
//
//   ioctl(fd_, SPIIOC_TRANSFER, (unsigned long)((uintptr_t)&seq_));

    /////////////////////////////////////////////////////

    SPI spi_driver{};
    Lan9252 esc(spi_driver);

    uint8_t txdata[40] = {0};
    uint8_t rxdata[40] = {0};
    memset(txdata, 0, 40);
    // memcpy(txdata, &data, 4);

//    txdata[0] = 0x02;
//    txdata[1] = 0x01;
//    txdata[2] = 0xF8;
//    txdata[3] = 0x01;
//
//    spi_driver.init();
//    spi_driver.enableChipSelect();
//    spi_driver.transfer(txdata, rxdata, 7);
//    spi_driver.disableChipSelect();
//
//    memset(txdata, 0, 40);
//    txdata[0] = 0x03;
//    txdata[1] = 0x01;
//    txdata[2] = 0xF8;
//    spi_driver.enableChipSelect();
//    spi_driver.transfer(txdata, rxdata, 3);
//
//    memset(txdata, 0, 40);
//    spi_driver.transfer(txdata, rxdata, 4);
//    spi_driver.disableChipSelect();

    reportError(esc.init());

    uint16_t al_status;
    reportError(esc.read(AL_STATUS, &al_status, sizeof(al_status)));
    printf("Al status %x \n", al_status);

    uint16_t station_alias;
    reportError(esc.read(0x0012, &station_alias, sizeof(station_alias)));
    printf("before write station_alias %x \n", station_alias);

    station_alias = 0xCAFE;
    reportError(esc.write(0x0012, &station_alias, sizeof(station_alias)));
    printf("Between read station alias \n");
    reportError(esc.read(0x0012, &station_alias, sizeof(station_alias)));
    printf("after station_alias %x \n", station_alias);


    while(true)
    {
        esc_routine(esc);
        usleep(1000);
    }

    return 0;
}





