#ifndef SLAVE_STACK_DRIVERS_ARDUINO_SPI_H_
#define SLAVE_STACK_DRIVERS_ARDUINO_SPI_H_

#include "kickcat/AbstractSPI.h"
#include <SPI.h>

// TODO improve. Name in lowercase to avoid conflict with SPI arduino lib.

namespace kickcat
{
    const uint32_t CS_PIN = 9; //default pin for chip select.

    const uint32_t DUMMY_BYTE = 0xFF;

    const uint32_t SPI_SPEED = 8000000; // Hz
    const SPISettings SETTINGS(SPI_SPEED, MSBFIRST, SPI_MODE0);

    struct Address
    {
        uint8_t byte0;
        uint8_t byte1;
    };

    int test();

    class spi : public AbstractSPI
    {
    public:
        spi() = default;
        ~spi();

        void init() override;
        uint8_t read() override;
        void write(uint8_t data) override;

        void enableChipSelect() {digitalWrite(CS_PIN, LOW);};
        void disableChipSelect() {digitalWrite(CS_PIN, HIGH);};

        void beginTransaction();
        void endTransaction();
    };


}
#endif
