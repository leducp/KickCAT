#ifndef SLAVE_STACK_INCLUDE_RPI_SPI_H_
#define SLAVE_STACK_INCLUDE_RPI_SPI_H_

#include "kickcat/AbstractSPI.h"
#include <string>
#include <map>
#include <bcm2835.h>

namespace kickcat
{
    constexpr uint32_t CORE_CLOCK_HZ = 250'000'000;

    const std::map<uint32_t, bcm2835SPIClockDivider> CLOCK_TABLE = {
        {2, BCM2835_SPI_CLOCK_DIVIDER_2},
        {4, BCM2835_SPI_CLOCK_DIVIDER_4},
        {8, BCM2835_SPI_CLOCK_DIVIDER_8},
        {16, BCM2835_SPI_CLOCK_DIVIDER_16},
        {32, BCM2835_SPI_CLOCK_DIVIDER_32},
        {64, BCM2835_SPI_CLOCK_DIVIDER_64},
        {128, BCM2835_SPI_CLOCK_DIVIDER_128},
        {256, BCM2835_SPI_CLOCK_DIVIDER_256},
        {512, BCM2835_SPI_CLOCK_DIVIDER_512},
        {1024, BCM2835_SPI_CLOCK_DIVIDER_1024},
        {2048, BCM2835_SPI_CLOCK_DIVIDER_2048},
        {4096, BCM2835_SPI_CLOCK_DIVIDER_4096},
        {8192, BCM2835_SPI_CLOCK_DIVIDER_8192},
        {16384, BCM2835_SPI_CLOCK_DIVIDER_16384},
        {32768, BCM2835_SPI_CLOCK_DIVIDER_32768},
    };

    constexpr uint8_t SPI_MODES[2][2] = {
        {BCM2835_SPI_MODE0, BCM2835_SPI_MODE1},
        {BCM2835_SPI_MODE2, BCM2835_SPI_MODE3},
    };
    
    class SPI final : public AbstractSPI
    {
    public:
        SPI();
        virtual ~SPI();

        void open(std::string const& device, uint8_t CPOL, uint8_t CPHA, uint32_t baudrate) override;
        void close() override;

        void transfer(uint8_t const* data_write, uint8_t* data_read, uint32_t size) override;

        void enableChipSelect() override;
        void disableChipSelect() override;

        void setMode(uint8_t CPOL, uint8_t CPHA) override;
        void setBaudRate(uint32_t baudrate) override;
    };
}

#endif
