#include "kickcat/ESC/Lan9252.h"

#include <cstring>
#include <inttypes.h>

namespace kickcat
{
    Lan9252::Lan9252(std::shared_ptr<AbstractSPI> spi_interface)
    : spi_interface_(spi_interface)
    {
    }

    hresult Lan9252::init()
    {
        spi_interface_->init();
        spi_interface_->disableChipSelect();

        printf("init lan \n");

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
          printf("Timeout reset \n");
          return hresult::E_ETIMEDOUT;
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
          printf("Timeout get byte test \n");
          return hresult::E_ETIMEDOUT;
        }
        printf("Byte test read: %" PRIu32 " \n", byte_test_result);


        uint32_t hw_cfg_ready = 0;
        counter = 0;
        while (counter < timeout and not (hw_cfg_ready & DEVICE_READY))
        {
            counter++;
            readInternalRegister(HW_CFG, hw_cfg_ready);
        }

        if (counter == timeout)
        {
          printf("Timeout hw cfg ready \n");
          return hresult::E_ETIMEDOUT;
        }
        return hresult::OK;
    }


    hresult Lan9252::waitCSR()
    {
        uint32_t esc_status;
        nanoseconds start_time = since_epoch();
        do
        {
            readInternalRegister(ECAT_CSR_CMD, esc_status);
            if (elapsed_time(start_time) > TIMEOUT)
            {
                return hresult::E_ETIMEDOUT;
            }
        }
        while(esc_status & ECAT_CSR_BUSY);
        return hresult::OK;
    }


    void Lan9252::readInternalRegister(uint16_t address, void* payload, uint16_t size)
    {
        InternalRegisterControl cmd{READ, hton<uint16_t>(address), {}};

        spi_interface_->enableChipSelect();
        spi_interface_->write(&cmd, CSR_CMD_HEADER_SIZE);

        spi_interface_->read(payload, size);
        spi_interface_->disableChipSelect();
    }


    void Lan9252::writeInternalRegister(uint16_t address, void const* payload, uint16_t size)
    {
        InternalRegisterControl cmd{WRITE, hton<uint16_t>(address), {}};
        memcpy(cmd.payload, payload, size);
        spi_interface_->enableChipSelect();
        spi_interface_->write(&cmd, CSR_CMD_HEADER_SIZE + size);
        spi_interface_->disableChipSelect();
    }


    int32_t Lan9252::readData(uint16_t address, void* data, uint16_t to_read)
    {
        uint16_t size = std::min(to_read, static_cast<uint16_t>(4));

        if (size == 3)
        {
            size = 2;
        }

        writeInternalRegister(ECAT_CSR_CMD, CSR_CMD{address, static_cast<uint8_t>(size), CSR_CMD::ESC_READ});
        hresult rc = waitCSR();
        if (rc != hresult::OK)
        {
            printf("Timeout\n");
            return -1;
        }

        readInternalRegister(ECAT_CSR_DATA, data, size);

        return size;
    }


    hresult Lan9252::read(uint16_t address, void* data, uint16_t size)
    {
        if (address < 0x1000)
        {

            uint16_t to_read = size;
            uint8_t* bufferPos = static_cast<uint8_t*>(data);

            while (to_read > 0)
            {
                uint16_t offset = size - to_read;
                int32_t readBytes = readData(address + offset, bufferPos + offset, to_read);
                if (readBytes < 0)
                {
                    return hresult::E_ETIMEDOUT;
                }

                if (readBytes == 0)
                {
                    return hresult::E_ENOMEM;
                }
                to_read -= readBytes;
            }
        }
        else if (address + size < 0x2000)
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
                    return hresult::E_ETIMEDOUT;
                }
            } while(fifo_slot_available != size / 4);  // beware assumption ESC will transfer 32 bytes.

            readInternalRegister(ECAT_PRAM_RD_DATA, data, size);
        }
        else
        {
            return hresult::E_ERANGE;
        }
        return hresult::OK;
    }


    int32_t Lan9252::writeData(uint16_t address, void const* data, uint16_t to_write)
    {
        uint16_t size = std::min(to_write, static_cast<uint16_t>(4));

        if (size == 3)
        {
            size = 2;
        }

        // CSR_DATA is 4 bytes
        uint32_t padding = 0;
        memcpy(&padding, data, size);
        writeInternalRegister(ECAT_CSR_DATA, data, sizeof(padding));
        writeInternalRegister(ECAT_CSR_CMD, CSR_CMD{address, static_cast<uint8_t>(size), CSR_CMD::ESC_WRITE});
        // wait for command execution
        hresult rc = waitCSR();
        if (rc != hresult::OK)
        {
            printf("Timeout\n");
            return -1;
        }

        return size;
    }


    hresult Lan9252::write(uint16_t address, void const* data, uint16_t size)
    {
        if (address < 0x1000)
        {
            if (size > 2)
            {
                return hresult::E_ERANGE;
            }

            uint16_t to_write = size;
            uint8_t const* bufferPos = static_cast<uint8_t const*>(data);

            while (to_write > 0)
            {
                uint16_t offset = size - to_write;
                int32_t writenBytes = writeData(address + offset, bufferPos + offset, to_write);
                if (writenBytes < 0)
                {
                    return hresult::E_ETIMEDOUT;
                }

                if (writenBytes == 0)
                {
                    return hresult::E_ENOMEM;
                }
                to_write -= writenBytes;
            }
        }
        else if (address + size < 0x2000)
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
            } while(fifo_slot_available < size / 4);  // beware assumption all the data fit in the the 64 bytes fifo.

            writeInternalRegister(ECAT_PRAM_WR_DATA, data, size);
        }
        else
        {
            return hresult::E_ERANGE;
        }

        return hresult::OK;
    }
}
