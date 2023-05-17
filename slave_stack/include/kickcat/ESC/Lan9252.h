#ifndef SLAVE_STACK_INCLUDE_ESC_LAN9252_H_
#define SLAVE_STACK_INCLUDE_ESC_LAN9252_H_

#include "kickcat/AbstractESC.h"
#include "kickcat/AbstractSPI.h"

#include "spi.h"

namespace kickcat
{
    // Commands
    const uint8_t CMD_SPI_READ = 0x03;
    const uint8_t CMD_SPI_WRITE = 0x02;
    const uint8_t BYTE_TEST = 0x64;

    // Registers
    const uint8_t PRAM_READ_LEN = 0x308;
    const uint8_t RESET_CTL     = 0x01F8;      // reset register
    const uint8_t ECAT_CSR_DATA = 0x0300;      // EtherCAT CSR Interface Data Register
    const uint8_t ECAT_CSR_CMD  = 0x0304;      // EtherCAT CSR Interface Command Register

    // Flags
    const uint8_t   ECAT_CSR_BUSY = 0x80;
    const uint16_t  PRAM_ABORT    = 0x40000000;
    const uint8_t   PRAM_BUSY     = 0x80;
    const uint8_t   PRAM_AVAIL    = 0x01;
    const uint8_t   READY         = 0x08;

    const uint32_t  DIGITAL_RST   = 0x00000001;


    const uint32_t BYTE_TEST_DEFAULT = 0x87654321;

    class Lan9252 : public AbstractESC
    {
    public:
        Lan9252() = default;
        ~Lan9252() = default;

        void init() override;

        // Return error code based on availability of the requested register
        virtual int32_t readRegister(uint16_t address, uint32_t& data, uint8_t size) override;
        virtual int32_t writeRegister(uint16_t address, uint32_t data, uint8_t size) override;

        virtual void readPDO(uint8_t* data, uint32_t size) override;
        virtual void writePDO(uint8_t* data, uint32_t size) override;

        virtual int32_t readEEPROM(uint8_t* data, uint32_t size) override;
        virtual int32_t writeEEPROM(uint8_t* data, uint32_t size) override;


        /// \brief Read given register.
        /// \param address of the register, will be split into two bytes
        /// \param data to be read
        /// \param size number of bytes to read, max 4.
        /// \return error code
        int32_t readRegisterDirect(uint16_t address, uint32_t& data, uint8_t size);
        int32_t writeRegisterDirect(uint16_t address, uint32_t data);

        int32_t readRegisterIndirect(uint16_t address, uint32_t& data, uint8_t size);
        int32_t writeRegisterIndirect(uint16_t address, uint32_t data, uint8_t size);

    private:
        spi spi_interface_; // TODO injection ? AbstractInterface at abstractESC constructor ? AbstractPDI ?
    };


}


#endif
