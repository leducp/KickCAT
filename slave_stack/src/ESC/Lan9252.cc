#include "kickcat/ESC/Lan9252.h"

namespace kickcat
{
    ErrorCode Lan9252::init()
    {
        spi_interface_.init();
        spi_interface_.disableChipSelect();
        pinMode(CS_PIN, OUTPUT);
        delay(100);

        Serial.println("init lan");

        spi_interface_.beginTransaction();


        writeInternalRegister(RESET_CTL, DIGITAL_RST);

        uint16_t counter = 0;
        uint16_t timeout = 10000;

        // wait for reset to complete
        uint32_t reset_ctl_value = 1;

        while (counter < timeout and (reset_ctl_value & 0x1))
        {
            counter++;
            readInternalRegister(RESET_CTL, reset_ctl_value);
        }

        if (counter == timeout)
        {
          Serial.println("Timeout reset");
          return ErrorCode::ETIMEDOUT;
        }

        // Check SPI interface is ready thanks to BYTE_TEST to test byte order
        uint32_t byte_test_result = 0;
        counter = 0;
        while (counter < timeout and  byte_test_result != BYTE_TEST_DEFAULT)
        {
            counter++;
            readInternalRegister(BYTE_TEST, byte_test_result);
        }

        if (counter == timeout)
        {
          Serial.println("Timeout get byte test");
          return ErrorCode::ETIMEDOUT;
        }
        Serial.print("Byte test read: ");
        Serial.println(byte_test_result, HEX);


        uint32_t hw_cfg_ready = 0;
        counter = 0;
        while (counter < timeout and not (hw_cfg_ready & DEVICE_READY))
        {
            counter++;
            readInternalRegister(HW_CFG, hw_cfg_ready);
        }

        if (counter == timeout)
        {
          Serial.println("Timeout hw cfg ready");
          return ErrorCode::ETIMEDOUT;
        }

        spi_interface_.endTransaction();

        return ErrorCode::OK;
    }


    ErrorCode Lan9252::waitCSRReady()
    {
        uint32_t esc_status;
        nanoseconds start_time = since_epoch();
        do
        {
            readInternalRegister(ECAT_CSR_CMD, esc_status);
            if (elapsed_time(start_time) > TIMEOUT)
            {
                return ErrorCode::ETIMEDOUT;
            }
        }
        while(esc_status & ECAT_CSR_BUSY);
        return ErrorCode::OK;
    }


    void Lan9252::readInternalRegister(uint16_t address, void* payload, uint32_t size)
    {
        InternalRegisterControl cmd{READ, hton(address), {}};

        spi_interface_.enableChipSelect();
        spi_interface_.write(&cmd, CSR_CMD_HEADER_SIZE);

        spi_interface_.read(payload, size);
        spi_interface_.disableChipSelect();
    }


    void Lan9252::writeInternalRegister(uint16_t address, void const* payload, uint32_t size)
    {
        InternalRegisterControl cmd{WRITE, hton(address), {}};
        memcpy(cmd.payload, payload, size);
        spi_interface_.enableChipSelect();
        spi_interface_.write(&cmd, CSR_CMD_HEADER_SIZE + size);
        spi_interface_.disableChipSelect();
    }


    ErrorCode Lan9252::readRegister(uint16_t address, void* data, uint32_t size)
    {
        if (address < 0x1000)
        {
            if (size > 4 or size == 3)
            {
                return ErrorCode::ERANGE;
            }

            writeInternalRegister(ECAT_CSR_CMD, CSR_CMD{address, size, CSR_CMD::ESC_READ});
            waitCSRReady(); //TODO timeout
            readInternalRegister(ECAT_CSR_DATA, data, size);
        }
        else if (address < 0x2000)
        {
            writeInternalRegister(ECAT_PRAM_RD_CMD, PRAM_ABORT);
            uint32_t addr_len = address | (size << 16);             // check size alignment and max value.
            writeInternalRegister(ECAT_PRAM_RD_ADDR_LEN, addr_len);
            writeInternalRegister(ECAT_PRAM_RD_CMD, PRAM_BUSY);  // order start read

            uint16_t fifo_slot_available; // slot of 4 bytes
            nanoseconds start_time = since_epoch();
            do
            {
                readInternalRegister(ECAT_PRAM_RD_CMD, fifo_slot_available);
                fifo_slot_available = fifo_slot_available >> 8;
                if (elapsed_time(start_time) > TIMEOUT)
                {
                    return ErrorCode::ETIMEDOUT;
                }
            } while(fifo_slot_available != size / 4);  // TODO beware assumption ESC will transfer 32 bytes.

            readInternalRegister(ECAT_PRAM_RD_DATA, data, size);
        }

        return ErrorCode::OK;
    };

    ErrorCode Lan9252::writeRegister(uint16_t address, void const* data, uint32_t size)
    {
        if (address < 0x1000)
        {
            if (size > 4 or size == 3)
            {
                return ErrorCode::ERANGE;
            }
            // CSR_DATA is 4 bytes
            uint32_t padding = 0;
            memcpy(&padding, data, size);
            writeInternalRegister(ECAT_CSR_DATA, data, sizeof(padding));
            writeInternalRegister(ECAT_CSR_CMD, CSR_CMD{address, size, CSR_CMD::ESC_WRITE});
            // wait for command execution
            waitCSRReady();
        }
        else if (address < 0x2000)
        {
            writeInternalRegister(ECAT_PRAM_WR_CMD, PRAM_ABORT);
            uint32_t addr_len = address | (size << 16);             // check size alignment and max value.
            writeInternalRegister(ECAT_PRAM_WR_ADDR_LEN, addr_len);
            writeInternalRegister(ECAT_PRAM_WR_CMD, PRAM_BUSY);  // order start write

            uint16_t fifo_slot_available; // slot of 4 bytes
            do
            {
                readInternalRegister(ECAT_PRAM_WR_CMD, fifo_slot_available);
                fifo_slot_available = fifo_slot_available >> 8;
            } while(fifo_slot_available < size / 4);  // TODO beware assumption all the data fit in the the 64 bytes fifo.

            writeInternalRegister(ECAT_PRAM_WR_DATA, data, size);
        }
        else
        {
            // Out of ram range
        }

        return ErrorCode::OK;
    }

//    int32_t Lan9252::readEEPROM(uint8_t* data, uint32_t size)
//    {
//        return 0;
//    }
//
//
//    int32_t Lan9252::writeEEPROM(uint8_t* data, uint32_t size)
//    {
//        return 0;
//    }

}
