#include "spi.h"

namespace kickcat
{
    spi::~spi()
    {
        SPI.end();
    }

    void spi::init()
    {
        SPI.begin();
    }

    void spi::transfer(uint8_t const* data_write, uint8_t* data_read, uint32_t size)
    {
//        Serial.print("transfer size: ");
//        Serial.println(size);
        if (data_write == nullptr and data_read == nullptr)
        {
            return;
        }

        if (data_write == nullptr)
        {
//            Serial.println("READ spi");
            for (uint32_t i = 0; i<size; i++)
            {
                data_read[i] = SPI.transfer(DUMMY_BYTE);
//                Serial.print("    i: ");
//                Serial.print(i);
//                Serial.print(" data read ");
//                Serial.println(data_read[i], HEX);
            }
        }

        if (data_read == nullptr)
        {
//            Serial.println("WRITE spi");
            for (uint32_t i = 0; i<size; i++)
            {
                SPI.transfer(data_write[i]);
//                Serial.print("    i: ");
//                Serial.print(i);
//                Serial.print(" data write ");
//                Serial.println(data_write[i], HEX);
            }
        }
    }



//    uint8_t spi::read()
//    {
//        return SPI.transfer(DUMMY_BYTE);
//    }
//
//
//    void spi::write(uint8_t data)
//    {
//        SPI.transfer(data);
//    }


    void spi::beginTransaction()
    {
        SPI.beginTransaction(SETTINGS);
    }


    void spi::endTransaction()
    {
        SPI.endTransaction();
    }
}
