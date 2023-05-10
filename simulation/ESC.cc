#include "ESC.h"
#include "kickcat/Time.h"

#include <cstring>
#include <fstream>

namespace kickcat
{
    ESC::ESC(std::string const& eeprom)
        : registers_{}
        , station_address_{reinterpret_cast<uint16_t*>(registers_ + reg::STATION_ADDR)}
    {
        // Device emulation is ON
        registers_[reg::ESC_CONFIG] = 0x01;

        // Default value is 'Request INIT State'
        registers_[reg::AL_CONTROL + 0] = 0x01;
        registers_[reg::AL_CONTROL + 1] = 0x00;

        // Default value is 'INIT State'
        registers_[reg::AL_STATUS + 0] = 0x01;
        registers_[reg::AL_STATUS + 1] = 0x00;

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
                if (position == *station_address_)
                {
                    processReadCommand(header, data, wkc, offset);
                }
                break;
            }
            case Command::FPWR:
            {
                if (position == *station_address_)
                {
                    processWriteCommand(header, data, wkc, offset);
                }
                break;
            }
            case Command::FPRW:
            {
                if (position == *station_address_)
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
        registers_[reg::AL_STATUS] =  registers_[reg::AL_CONTROL];

        // Handle eeprom access
        uint16_t order = registers_[reg::EEPROM_CONTROL + 1] << 8;
        switch (order)
        {
            case eeprom::Command::READ:
            {
                int32_t address = registers_[reg::EEPROM_ADDRESS] + (registers_[reg::EEPROM_ADDRESS + 1] << 8);
                std::memcpy(registers_ + reg::EEPROM_DATA, eeprom_.data() + address, 4);
                registers_[reg::EEPROM_CONTROL + 1] &= ~7; // clear order - TODO 7 -> mask
                eeprom_busy_latency = since_epoch() + 2us;
                break;
            }
            case eeprom::Command::WRITE:
            {
                int32_t address = registers_[reg::EEPROM_ADDRESS] + (registers_[reg::EEPROM_ADDRESS + 1] << 8);
                std::memcpy(eeprom_.data() + address, data, 4);
                registers_[reg::EEPROM_CONTROL + 1] &= ~7; // clear order
                eeprom_busy_latency = since_epoch() + 2us;
                break;
            }
            case eeprom::Command::NOP:
            case eeprom::Command::RELOAD:
            {
                break;
            }
        }

        // Set/clear eeprom busy bit
        if (since_epoch() < eeprom_busy_latency)
        {
            registers_[reg::EEPROM_CONTROL + 1] |= 0x80;
        }
        else
        {
            registers_[reg::EEPROM_CONTROL + 1] &= ~0x80;
            eeprom_busy_latency = 0ns;
        }
    }


    void ESC::processReadCommand(DatagramHeader* header, uint8_t* data, uint16_t* wkc, uint16_t offset)
    {
        std::memcpy(data, registers_ + offset, header->len);
        *wkc += 1;
    }

    void ESC::processWriteCommand(DatagramHeader* header, uint8_t* data, uint16_t* wkc, uint16_t offset)
    {
        std::memcpy(registers_ + offset, data, header->len);
        *wkc += 1;
    }

    void ESC::processReadWriteCommand(DatagramHeader* header, uint8_t* data, uint16_t* wkc, uint16_t offset)
    {
        uint8_t swap[0x1000];

        std::memcpy(swap, registers_ + offset, header->len);
        std::memcpy(registers_ + offset, data, header->len);
        std::memcpy(data, swap, header->len);
        *wkc += 3;
    }
}