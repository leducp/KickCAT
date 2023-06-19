#ifndef SLAVE_STACK_INCLUDE_ESC_LAN9252_H_
#define SLAVE_STACK_INCLUDE_ESC_LAN9252_H_

#include "kickcat/AbstractESC.h"
#include "kickcat/AbstractSPI.h"

#include "spi.h"


namespace kickcat
{
    // Host to Network byte order helper (Reminder: EtherCAT is LE, network is BE)

    // TODO do not duplicate, used it from protocol.h
// modify because arduino does not support C++ 17 by default
    template<typename T>
    T hton(T value)
    {
        if (sizeof(T) == 2)
        {
            return T((value << 8) | ((value >> 8) & 0xff));
        }
        else if (sizeof(T) == 4)
        {
            return T((value << 24) | ((value << 8) & 0x00ff0000) | ((value >> 8) & 0x0000ff00) | ((value >> 24) & 0xff));
        }
        else
        {
            printf("hton size unsupported \n");
        }
    }


    // SPI INSTRUCTIONS
    const uint8_t READ  = 0x03;
    const uint8_t WRITE = 0x02;

    const uint8_t BYTE_TEST = 0x64;

    // Registers
    const uint16_t PRAM_READ_LEN = 0x308;
    const uint16_t RESET_CTL     = 0x01F8;      // reset register
    const uint16_t ECAT_CSR_DATA = 0x0300;      // EtherCAT CSR Interface Data Register
    const uint16_t ECAT_CSR_CMD  = 0x0304;      // EtherCAT CSR Interface Command Register
    const uint16_t HW_CFG        = 0x0074;      // Is device ready

    // Ethercat registers missing from protocol.h TODO check where put them.
    const uint16_t AL_EVENT            =   0x0220;      // AL event request
    const uint16_t AL_EVENT_MASK       =   0x0204;      // AL event interrupt mask

    // In protocol but can't access to it now
    const uint16_t WDOG_STATUS         =   0x0440;      // watch dog status
    const uint16_t AL_STATUS           =   0x0130;      // AL status

    //--- ESC commands --------------------------------------------------------------------------------


    // Flags
    const uint32_t  ECAT_CSR_BUSY = 0x1 << 31;
    const uint32_t  DEVICE_READY =  0x1 << 27;
    const uint16_t  PRAM_ABORT    = 0x40000000;
    const uint8_t   PRAM_BUSY     = 0x80;
    const uint8_t   PRAM_AVAIL    = 0x01;
    const uint8_t   READY         = 0x08;

    const uint32_t  DIGITAL_RST   = 0x01;


    const uint32_t BYTE_TEST_DEFAULT = 0x87654321;



    const uint8_t CSR_CMD_HEADER_SIZE = 3;
    struct InternalRegisterControl
    {
        uint8_t  instruction;               // Read / write // TODO enum full.
        uint16_t LAN9252_register_address;  // address of SYSTEM CONTROL AND STATUS REGISTERS
        uint8_t  payload[0x1C];             // Max payload size is 1C bytes.
    } __attribute__((__packed__));


    struct CSR_CMD
    {
        static constexpr uint8_t ESC_WRITE = 0x80;
        static constexpr uint8_t ESC_READ  = 0xC0;

        uint16_t ethercat_register_address;
        uint8_t  ethercat_register_size; //1,2,4
        uint8_t  ethercat_register_operation; // read / write
    } __attribute__((__packed__));


    class Lan9252 : public AbstractESC
    {
    public:
        Lan9252() = default;
        ~Lan9252() = default;

        void init() override;

        // Return error code based on availability of the requested register
        virtual int32_t readRegister(uint16_t address, void* data, uint32_t size) override;


        virtual int32_t writeRegister(uint16_t address, void const* data, uint32_t size) override;

//        virtual void readPDO(uint8_t* data, uint32_t size) override;
//        virtual void writePDO(uint8_t* data, uint32_t size) override;
//
//        virtual int32_t readEEPROM(uint8_t* data, uint32_t size) override;
//        virtual int32_t writeEEPROM(uint8_t* data, uint32_t size) override;
//
//
//        /// \brief Read given register.
//        /// \param address of the register, will be split into two bytes
//        /// \param data to be read
//        /// \param size number of bytes to read, max 4.
//        /// \return error code
//        int32_t readRegisterDirect(uint16_t address, uint32_t& data, uint8_t size);
//        int32_t writeRegisterDirect(uint16_t address, uint32_t data);
//
//        /// \brief Read given register.
//        /// \param address of the register, will be split into two bytes
//        /// \param data to be read
//        /// \param size number of bytes to read, allowed values: 1, 2, 4.
//        /// \return error code
//        int32_t readRegisterIndirect(uint16_t address, uint32_t& data, uint8_t size);
//        int32_t writeRegisterIndirect(uint16_t address, uint32_t data, uint8_t size);

    private:
        template <typename T>
        void readInternalRegister(uint16_t address, T& payload)
        {
            readInternalRegister(address, &payload, sizeof(payload));
        };

        void readInternalRegister(uint16_t address, void* payload, uint32_t size);

        template <typename T>
        void writeInternalRegister(uint16_t address, T const& payload)
        {
            writeInternalRegister(address, &payload, sizeof(payload));
        };

        void writeInternalRegister(uint16_t address, void const* payload, uint32_t size);

        int32_t waitCSRReady();


        spi spi_interface_; // TODO injection ? AbstractInterface at abstractESC constructor ? AbstractPDI ?
    };


}


#endif
