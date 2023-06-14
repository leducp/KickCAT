#include "kickcat/ESC/Lan9252.h"

namespace kickcat
{
    void Lan9252::init()
    {
        spi_interface_.init();
        spi_interface_.disableChipSelect();
        pinMode(CS_PIN, OUTPUT);
        delay(100);

        delay(1000);
        Serial.println("init lan");

        spi_interface_.beginTransaction();

//        CSR_CMD reset_ctl{WRITE, hton(RESET_CTL), {DIGITAL_RST}};
//        uint8_t* to_send = &reset_ctl;
//        spi_interface_.write(to_send, 5);  // possible reduce with void*, possible helper write/read command.

        writeCommand(RESET_CTL, DIGITAL_RST);

//        writeRegisterDirect(RESET_CTL, DIGITAL_RST);

        // Check SPI interface is ready thanks to BYTE_TEST
        uint16_t counter = 0;
        uint16_t timeout = 10000;

        uint32_t byte_test_result = 0;

        while (counter < timeout and  byte_test_result != BYTE_TEST_DEFAULT)
        {
            counter++;
//            CSR_CMD read_byte_test{READ, hton(BYTE_TEST), {}};
//            uint8_t* to_send_read = &read_byte_test;
//            spi_interface_.write(to_send_read, 4);
//
//            spi_interface_.read(reinterpret_cast<void*>(byte_test_result), sizeof(byte_test_result));
            readCommand(BYTE_TEST, byte_test_result);


//            readRegisterDirect(BYTE_TEST, byte_test_result, sizeof(byte_test_result));
        }

        if (counter == timeout)
        {
          Serial.println("Timeout get byte test");
        }

        Serial.print("Byte test read: ");
        Serial.println(byte_test_result, HEX);

        spi_interface_.endTransaction();
    }


    int32_t Lan9252::readRegister(uint16_t address, uint32_t& data, uint8_t size)
    {
        return 0;
    }


    int32_t Lan9252::writeRegister(uint16_t address, uint32_t data, uint8_t size)
    {
        return 0;
    }


//    int32_t Lan9252::readRegisterDirect(uint16_t address, uint32_t& data, uint8_t size)
//    {
//        uint8_t address_byte0 = address;
//        uint8_t address_byte1 = address >> 8;
//
//        if (size > 4)
//        {
//            return -1; // Invalid param
//        }
//        spi_interface_.enableChipSelect();
//
//        spi_interface_.write(CMD_SPI_READ);
//        spi_interface_.write(address_byte1);
//        spi_interface_.write(address_byte0);
//
//        uint8_t buffer[4] = {0,0,0,0};
//        for (uint8_t i = 0; i < size; i++)
//        {
//            buffer[i] = spi_interface_.read();
//        }
//
//        memcpy(&data, buffer, sizeof(data));
//
//        spi_interface_.disableChipSelect();
//        return 0;
//    }
//
//
//    int32_t Lan9252::writeRegisterDirect(uint16_t address, uint32_t data)
//    {
//        uint8_t address_byte0 = address;
//        uint8_t address_byte1 = address >> 8;
//
//        spi_interface_.enableChipSelect();
//
//        spi_interface_.write(CMD_SPI_WRITE);
//        spi_interface_.write(address_byte1);
//        spi_interface_.write(address_byte0);
//
//        for (uint8_t i = 0; i < 4; i++)
//        {
//            uint8_t to_write = data >> 8*i;
//            spi_interface_.write(to_write);
//        }
//
//        spi_interface_.disableChipSelect();
//        return 0;
//    }
//
//
//
//    int32_t Lan9252::readRegisterIndirect(uint16_t address, uint32_t& data, uint8_t size)
//    {
//        waitCSRReady();
//
//        uint8_t csr_cmd[4];
//        csr_cmd[0] = address;
//        csr_cmd[1] = address >> 8;
//        csr_cmd[2] = size;
//        csr_cmd[3] = ESC_READ;
//
//        uint32_t read_esc;
//        memcpy(&read_esc, csr_cmd, 4);
//        writeRegisterDirect(ECAT_CSR_CMD, read_esc);
//
//
//        return 0;
//    }


    int32_t Lan9252::waitCSRReady()
    {
        uint32_t esc_status;
        do
        {
            readCommand(ECAT_CSR_CMD, esc_status);
        }
        while(esc_status && ECAT_CSR_BUSY);
        return 0;
    }


    int32_t Lan9252::writeRegisterIndirect(uint16_t address, uint32_t data, uint8_t size)
    {

        return 0;
    }


    void Lan9252::readPDO(uint8_t* data, uint32_t size)
    {

    }


    void Lan9252::writePDO(uint8_t* data, uint32_t size)
    {

    }


    int32_t Lan9252::readEEPROM(uint8_t* data, uint32_t size)
    {
        return 0;
    }


    int32_t Lan9252::writeEEPROM(uint8_t* data, uint32_t size)
    {
        return 0;
    }

}
