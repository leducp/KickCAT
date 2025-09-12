#ifndef SLAVE_STACK_INCLUDE_NUTTX_SPI_H_
#define SLAVE_STACK_INCLUDE_NUTTX_SPI_H_

#include "kickcat/AbstractSPI.h"
#include <nuttx/spi/spi_transfer.h>
#include <string>

namespace kickcat
{
    class SPI : public AbstractSPI
    {
    public:
        SPI();
        ~SPI();

        void init() override;
        void transfer(uint8_t const* data_write, uint8_t* data_read, uint32_t size) override;

        void enableChipSelect() override;
        void disableChipSelect() override;

        void setBaudRate(uint32_t baud) override { baudRate_ = baud; }
        uint32_t getBaudRate() const override { return baudRate_; }

        void setMode(uint8_t mode) override { mode_ = mode; }
        uint8_t getMode() const override { return mode_; }

        void setDevice(const std::string& devicePath) override { devicePath_ = devicePath; }
        std::string getDevice() const override { return devicePath_; }

        void setChipSelect(uint32_t cs) override { chipSelect_ = cs; }
        uint32_t getChipSelect() const override { return chipSelect_; }

    private:
        file filep_spi_;
        int fd_;
        spi_dev_s** priv_spi_;

        std::string devicePath_;
        uint32_t baudRate_;
        uint8_t mode_;
        uint32_t chipSelect_;
    };
}

#endif
