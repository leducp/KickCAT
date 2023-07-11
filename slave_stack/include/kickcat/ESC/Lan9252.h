#ifndef SLAVE_STACK_INCLUDE_ESC_LAN9252_H_
#define SLAVE_STACK_INCLUDE_ESC_LAN9252_H_

#include "kickcat/AbstractESC.h"
#include "kickcat/AbstractSPI.h"
#include "kickcat/Time.h"

#include <memory>
#include "Arduino.h"

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
    constexpr uint8_t READ  = 0x03;
    constexpr uint8_t WRITE = 0x02;

    constexpr uint8_t BYTE_TEST = 0x64;

    // Registers
    constexpr uint16_t HW_CFG        = 0x0074;      // Is device ready
    constexpr uint16_t RESET_CTL     = 0x01F8;      // reset register
    constexpr uint16_t ECAT_CSR_DATA = 0x0300;      // EtherCAT CSR Interface Data Register
    constexpr uint16_t ECAT_CSR_CMD  = 0x0304;      // EtherCAT CSR Interface Command Register

    constexpr uint16_t ECAT_PRAM_RD_ADDR_LEN = 0X0308;
    constexpr uint16_t ECAT_PRAM_RD_CMD      = 0X030C;
    constexpr uint16_t ECAT_PRAM_WR_ADDR_LEN = 0X0310;
    constexpr uint16_t ECAT_PRAM_WR_CMD      = 0X0314;

    constexpr uint16_t ECAT_PRAM_RD_DATA     = 0x000;  // until 0x01C
    constexpr uint16_t ECAT_PRAM_WR_DATA     = 0x020;  // until 0x03C

    constexpr uint16_t NUM_BYTE_INPUT  = 32;
    constexpr uint16_t NUM_BYTE_OUTPUT = 32;

    // Ethercat registers missing from protocol.h TODO check where put them.
    constexpr uint16_t AL_EVENT            =   0x0220;      // AL event request
    constexpr uint16_t AL_EVENT_MASK       =   0x0204;      // AL event interrupt mask

    // In protocol but can't access to it now
    constexpr uint16_t WDOG_STATUS         =   0x0440;      // watch dog status
    constexpr uint16_t AL_STATUS           =   0x0130;      // AL status

    //--- ESC commands --------------------------------------------------------------------------------


    // Flags
    constexpr uint32_t ECAT_CSR_BUSY = 0x1 << 31;
    constexpr uint32_t DEVICE_READY  = 0x1 << 27;
    constexpr uint32_t PRAM_ABORT    = 0x1 << 30;
    constexpr uint32_t PRAM_BUSY     = 0x1 << 31;
    constexpr uint32_t PRAM_AVAIL    = 0x01;

    constexpr uint32_t DIGITAL_RST   = 0x01;

    constexpr uint32_t ESM_INIT        = 0x1;
    constexpr uint32_t ESM_BOOTSTRAP   = 0x3;
    constexpr uint32_t ESM_PRE_OP      = 0x2;
    constexpr uint32_t ESM_SAFE_OP     = 0x4;
    constexpr uint32_t ESM_OP          = 0x8;


    constexpr uint32_t BYTE_TEST_DEFAULT = 0x87654321;

    constexpr milliseconds TIMEOUT{10};


    constexpr uint8_t CSR_CMD_HEADER_SIZE = 3;
    struct InternalRegisterControl
    {
        uint8_t  instruction;               // Read / write // TODO enum full.
        uint16_t LAN9252_register_address;  // address of SYSTEM CONTROL AND STATUS REGISTERS
        uint8_t  payload[64];             // Max payload size is 64 bytes (fifo).
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
        Lan9252(AbstractSPI& spi_interface);
        ~Lan9252() = default;

        hresult init() override;

        hresult read(uint16_t address, void* data, uint16_t size) override;
        hresult write(uint16_t address, void const* data, uint16_t size) override;

    private:
        template <typename T>
        void readInternalRegister(uint16_t address, T& payload)
        {
            readInternalRegister(address, &payload, sizeof(payload));
        };

        void readInternalRegister(uint16_t address, void* payload, uint16_t size);

        template <typename T>
        void writeInternalRegister(uint16_t address, T const& payload)
        {
            writeInternalRegister(address, &payload, sizeof(payload));
        };

        void writeInternalRegister(uint16_t address, void const* payload, uint16_t size);

        hresult waitCSR();

        AbstractSPI& spi_interface_; // TODO shared ptr like link in bus.h
    };


}


#endif
