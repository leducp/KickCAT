#include "ESC.h"
#include "kickcat/Time.h"

#include <cstring>
#include <fstream>

namespace kickcat
{
    ESC::ESC(std::string const& eeprom)
        : registers_{}
    {
        // Device emulation is ON
        registers_.esc_configuration = 0x01;

        // Default value is 'Request INIT State'
        registers_.al_control = 0x0001;

        // Default value is 'INIT State'
        registers_.al_status = 0x0001;

        // eeprom never busy because the data are processed in sync with the request.
        registers_.eeprom_control &= ~0x8000;

        std::ifstream eeprom_file;
        eeprom_file.open(eeprom, std::ios::binary | std::ios::ate);
        if (not eeprom_file.is_open())
        {
            THROW_ERROR("Cannot load EEPROM");
        }
        int size = eeprom_file.tellg();
        eeprom_file.seekg (0, std::ios::beg);
        eeprom_.resize(size / 2); // vector of uint16_t so / 2 since the size is in byte
        eeprom_file.read((char*)eeprom_.data(), size);
        eeprom_file.close();
    }

    void ESC::processDatagram(DatagramHeader* header, uint8_t* data, uint16_t* wkc)
    {
        auto [position, offset] = extractAddress(header->address);
        switch (header->command)
        {
            case Command::NOP :
            {
                break;
            }

            case Command::APRD:
            {
                if (position == 0)
                {
                    processReadCommand(header, data, wkc, offset);
                }
                ++position;
                header->address = createAddress(position, offset);
                break;
            }
            case Command::APWR:
            {
                if (position == 0)
                {
                    processWriteCommand(header, data, wkc, offset);
                }
                ++position;
                header->address = createAddress(position, offset);
                break;
            }
            case Command::APRW:
            {
                if (position == 0)
                {
                    processReadWriteCommand(header, data, wkc, offset);
                }
                ++position;
                header->address = createAddress(position, offset);
                break;
            }
            case Command::FPRD:
            {
                if (position == registers_.station_address)
                {
                    processReadCommand(header, data, wkc, offset);
                }
                break;
            }
            case Command::FPWR:
            {
                if (position == registers_.station_address)
                {
                    processWriteCommand(header, data, wkc, offset);
                }
                break;
            }
            case Command::FPRW:
            {
                if (position == registers_.station_address)
                {
                    processReadWriteCommand(header, data, wkc, offset);
                }
                break;
            }
            case Command::BRD:
            {
                processReadCommand(header, data, wkc, offset);
                break;
            }
            case Command::BWR:
            {
                processWriteCommand(header, data, wkc, offset);
                break;
            }
            case Command::BRW:
            {
                processReadWriteCommand(header, data, wkc, offset);
                break;
            }
            case Command::LRD:
            {
                *wkc += 1;
                break;
            }
            case Command::LWR:
            {
                *wkc += 1;
                break;
            }
            case Command::LRW:
            {
                *wkc += 3;
                break;
            }
            case Command::ARMW: { break; }
            case Command::FRMW: { break; }
        }

        // Process registers internal management

        // Mirror AL_STATUS - Device Emulation
        registers_.al_status = registers_.al_control;

        // Handle eeprom access
        uint16_t order = registers_.eeprom_control & 0x0701;
        switch (order)
        {
            case eeprom::Command::READ:
            {
                std::memcpy((void*)&registers_.eeprom_data, eeprom_.data() + registers_.eeprom_address, 4);
                registers_.eeprom_control &= ~0x0700; // clear order
                break;
            }
            case eeprom::Command::WRITE:
            {
                std::memcpy(eeprom_.data() + registers_.eeprom_address, data, 4);
                registers_.eeprom_control &= ~0x0700; // clear order
                break;
            }
            case eeprom::Command::NOP:
            case eeprom::Command::RELOAD:
            {
                break;
            }
        }
    }


    void ESC::processReadCommand(DatagramHeader* header, uint8_t* data, uint16_t* wkc, uint16_t offset)
    {
        std::memcpy(data, (uint8_t*)&registers_ + offset, header->len);
        *wkc += 1;
    }

    void ESC::processWriteCommand(DatagramHeader* header, uint8_t* data, uint16_t* wkc, uint16_t offset)
    {
        std::memcpy((uint8_t*)&registers_ + offset, data, header->len);
        *wkc += 1;
    }

    void ESC::processReadWriteCommand(DatagramHeader* header, uint8_t* data, uint16_t* wkc, uint16_t offset)
    {
        uint8_t swap[0x1000];

        std::memcpy(swap, (uint8_t*)&registers_ + offset, header->len);
        std::memcpy((uint8_t*)&registers_ + offset, data, header->len);
        std::memcpy(data, swap, header->len);
        *wkc += 3;
    }
}