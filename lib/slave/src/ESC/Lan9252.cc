#include "kickcat/ESC/Lan9252.h"
#include "kickcat/debug.h"

#include <cstring>
#include <inttypes.h>

namespace kickcat
{
    Lan9252::Lan9252(std::shared_ptr<AbstractSPI> spi_interface)
    : spi_interface_(spi_interface)
    {
    }

    int32_t Lan9252::init()
    {
        spi_interface_->disableChipSelect();
        writeInternalRegister(RESET_CTL, DIGITAL_RST);
        
        const uint16_t timeout = 10000;
        
        // Wait for reset to complete
        uint32_t reset_ctl_value;
        if (!pollRegister(RESET_CTL, reset_ctl_value, [](uint32_t val) { return !(val & 0x1); }, timeout))
        {
            return -ETIMEDOUT;
        }
        
        // Check SPI interface is ready
        uint32_t byte_test_result;
        if (!pollRegister(BYTE_TEST, byte_test_result, [](uint32_t val) { return val == BYTE_TEST_DEFAULT; }, timeout))
        {
            return -ETIMEDOUT;
        }
        
        slave_info("Byte test read: %" PRIu32 " \n", byte_test_result);
        
        // Wait for device ready
        uint32_t hw_cfg_ready;
        if (!pollRegister(HW_CFG, hw_cfg_ready, [](uint32_t val) { return val & DEVICE_READY; }, timeout))
        {
            slave_error("Timeout hw cfg ready \n");
            return -ETIMEDOUT;
        }
        
        return 0;
    }

    template<typename Predicate>
    bool Lan9252::pollRegister(uint16_t reg, uint32_t& value, Predicate condition, uint16_t timeout_ms)
    {
        for (uint16_t i = 0; i < timeout_ms; i++)
        {
            readInternalRegister(reg, value);
            if (condition(value))
            {
                return true;
            }
        }
        return false;
    }


    int32_t Lan9252::waitCSR()
    {
        uint32_t esc_status;
        nanoseconds start_time = since_epoch();
        do
        {
            readInternalRegister(ECAT_CSR_CMD, esc_status);
            if (elapsed_time(start_time) > TIMEOUT)
            {
                return -ETIMEDOUT;
            }
        }
        while(esc_status & ECAT_CSR_BUSY);
        return 0;
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
        // align write to 32 bits
        uint16_t to_write = size;
        uint16_t remaining = size % 4;
        if (remaining != 0)
        {
            to_write += (4 - remaining);
        }

        InternalRegisterControl cmd{WRITE, hton<uint16_t>(address), {}};
        std::memcpy(cmd.payload, payload, size);
        spi_interface_->enableChipSelect();
        spi_interface_->write(&cmd, CSR_CMD_HEADER_SIZE + to_write);
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
        int rc = waitCSR();
        if (rc < 0)
        {
            return rc;
        }

        readInternalRegister(ECAT_CSR_DATA, data, size);

        return size;
    }


    int32_t Lan9252::read(uint16_t address, void* data, uint16_t size)
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
                    return readBytes;
                }

                if (readBytes == 0)
                {
                    return -ENOMEM;
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

            uint16_t to_read = size;
            uint8_t* buffer_pos = static_cast<uint8_t*>(data);

            uint16_t fifo_slot_available; // slot of 4 bytes
            nanoseconds start_time = since_epoch();
            do
            {
                readInternalRegister(ECAT_PRAM_RD_CMD, fifo_slot_available);
                fifo_slot_available = fifo_slot_available >> 8;
                if (elapsed_time(start_time) > TIMEOUT)
                {
                    return -ETIMEDOUT;
                }

                if (fifo_slot_available > 0)
                {
                    uint16_t available = fifo_slot_available * 4;    // FIFO entry size is 32bits
                    uint16_t to_do = std::min(available, to_read);
                    readInternalRegister(ECAT_PRAM_RD_DATA, buffer_pos, to_do);
                    buffer_pos += to_do;
                    to_read -= to_do;
                }
            } while(to_read > 0);
        }
        else
        {
            return -ERANGE;
        }

        return size;
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
        std::memcpy(&padding, data, size);
        writeInternalRegister(ECAT_CSR_DATA, data, sizeof(padding));
        writeInternalRegister(ECAT_CSR_CMD, CSR_CMD{address, static_cast<uint8_t>(size), CSR_CMD::ESC_WRITE});

        // wait for command execution
        int32_t rc = waitCSR();
        if (rc < 0)
        {
            return rc;
        }

        return size;
    }


    int32_t Lan9252::write(uint16_t address, void const* data, uint16_t size)
    {
        if (address < 0x1000)
        {
            if (size > 2)
            {
                return -ERANGE;
            }

            uint16_t to_write = size;
            uint8_t const* bufferPos = static_cast<uint8_t const*>(data);

            while (to_write > 0)
            {
                uint16_t offset = size - to_write;
                int32_t writtenBytes = writeData(address + offset, bufferPos + offset, to_write);
                if (writtenBytes < 0)
                {
                    return writtenBytes;
                }

                if (writtenBytes == 0)
                {
                    return -ENOMEM;
                }
                to_write -= writtenBytes;
            }
        }
        else if (address + size < 0x2000)
        {
            writeInternalRegister(ECAT_PRAM_WR_CMD, PRAM_ABORT);
            uint32_t addr_len = address | (size << 16);             // check size alignment and max value.
            writeInternalRegister(ECAT_PRAM_WR_ADDR_LEN, addr_len);
            writeInternalRegister(ECAT_PRAM_WR_CMD, PRAM_BUSY);  // order start write

            uint16_t to_write = size;
            uint8_t const* buffer_pos = static_cast<uint8_t const*>(data);

            uint16_t fifo_slot_available; // slot of 4 bytes
            do
            {
                readInternalRegister(ECAT_PRAM_WR_CMD, fifo_slot_available);
                fifo_slot_available = fifo_slot_available >> 8;

                if (fifo_slot_available > 0)
                {
                    uint16_t available = fifo_slot_available * 4;    // FIFO entry size is 32bits
                    uint16_t to_do = std::min(available, to_write);
                    writeInternalRegister(ECAT_PRAM_WR_DATA, buffer_pos, to_do);
                    buffer_pos += to_do;
                    to_write -= to_do;
                }

            } while(to_write > 0);
        }
        else
        {
            return -ERANGE;
        }

        return size;
    }
}
