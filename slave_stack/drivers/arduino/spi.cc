#include "spi.h"

namespace kickcat
{
    int test()
    {
        return 42;
    }

    spi::~spi()
    {
        SPI.end();
    }

    void spi::init()
    {
        SPI.begin();
    }


    uint8_t spi::read()
    {
        return SPI.transfer(DUMMY_BYTE);
    }


    void spi::write(uint8_t data)
    {
        SPI.transfer(data);
    }


    void spi::beginTransaction()
    {
        SPI.beginTransaction(SETTINGS);
    }


    void spi::endTransaction()
    {
        SPI.endTransaction();
    }
}
