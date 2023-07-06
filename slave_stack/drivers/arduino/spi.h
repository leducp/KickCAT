#ifndef SLAVE_STACK_DRIVERS_ARDUINO_SPI_H_
#define SLAVE_STACK_DRIVERS_ARDUINO_SPI_H_

#include "kickcat/AbstractSPI.h"
#include <SPI.h>

// TODO improve. Name in lowercase to avoid conflict with SPI arduino lib.

namespace kickcat
{
    const uint32_t CS_PIN = 9; //default pin for chip select.

    const uint8_t DUMMY_BYTE = 0xFF;

    const uint32_t SPI_SPEED = 8000000; // Hz
//    const uint32_t SPI_SPEED = 100000; // Hz, for scope investigation
    const SPISettings SETTINGS(SPI_SPEED, MSBFIRST, SPI_MODE0);

    struct Address
    {
        uint8_t byte0;
        uint8_t byte1;
    };

    class spi : public AbstractSPI
    {
    public:
        spi() = default;
        ~spi();

        void init() override;
        void transfer(uint8_t const* data_write, uint8_t* data_read, uint32_t size) override;

        void enableChipSelect() override {digitalWrite(CS_PIN, LOW);};
        void disableChipSelect() override {digitalWrite(CS_PIN, HIGH);};
    };


}
#endif
