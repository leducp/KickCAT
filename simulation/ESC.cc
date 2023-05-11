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

    std::tuple<uint8_t*, uint16_t> ESC::computeInternalMemoryAccess(uint16_t address, uint16_t size)
    {
        uint8_t* pos = nullptr;
        uint16_t to_copy = 0;
        if (address < 0x1000)
        {
            pos = reinterpret_cast<uint8_t*>(&registers_);
            pos += address;
            to_copy = std::min(size, uint16_t(0x1000 - address));
        }
        else
        {
            pos = reinterpret_cast<uint8_t*>(process_data_ram);
            pos += (address - 0x1000);
            to_copy = std::min(size, uint16_t(0x10000 - address));
        }
        return std::make_tuple(pos, to_copy);
    }

    void ESC::write(uint16_t address, void const* data, uint16_t size)
    {
        auto[pos, to_copy] = computeInternalMemoryAccess(address, size);
        std::memcpy(pos, data, to_copy); //TODO change API? return really written?
        //printf("write to %x %p (%p)\n", address, pos, process_data_ram);
    }

    void ESC::read(uint16_t address, void* data, uint16_t size)
    {
        auto[pos, to_copy] = computeInternalMemoryAccess(address, size);
        std::memcpy(data, pos, to_copy); //TODO change API? return really written?
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
                uint32_t start_logical_address = header->address;
                uint32_t end_logical_address = header->address + header->len;

                uint16_t work = 0;
                for (auto const& pdo : rx_pdos_)
                {
                    uint32_t address_min = std::max(start_logical_address, pdo.logical_address);
                    uint32_t address_max = std::min(end_logical_address, pdo.logical_address + pdo.size);
                    if (address_max < address_min)
                    {
                        continue; // no intersection
                    }
                    uint32_t to_copy = address_max - address_min;
                    uint32_t phys_offset = address_min - pdo.logical_address;
                    uint32_t frame_offset = address_min - start_logical_address;

                    std::memcpy(data + frame_offset, pdo.physical_address + phys_offset, to_copy);
                    work = 1;
                }

                *wkc += work;
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

        // ESM state change
        if (registers_.al_control != registers_.al_status)
        {
            State before  = static_cast<State>(registers_.al_status  & 0xf);
            State current = static_cast<State>(registers_.al_control & 0xf);
            printf("%s -> %s\n", toString(before), toString(current));

            switch (current)
            {
                case State::BOOT:
                {
                    break;
                }
                case State::INIT:
                {
                    break;
                }
                case State::PRE_OP:
                {
                    break;
                }
                case State::SAFE_OP:
                {
                    if (before == State::PRE_OP)
                    {
                        configurePDOs();
                    }
                    break;
                }
                case State::OPERATIONAL:
                {
                    break;
                }
                default:
                {

                }
            }
            changeState_(before, current);
        }

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

    void ESC::configurePDOs()
    {
        for (int i = 0; i < 16; ++i)
        {
            auto const& fmmu = registers_.fmmu[i];
            if (fmmu.activate == 0)
            {
                continue;
            }

            PDO pdo;
            pdo.size = fmmu.length;
            pdo.logical_address = fmmu.logical_address;
            pdo.physical_address = process_data_ram + (fmmu.physical_address - 0x1000);

            if (fmmu.type == 1)
            {
                tx_pdos_.push_back(pdo);
            }
            else if (fmmu.type == 2)
            {
                rx_pdos_.push_back(pdo);
            }
            else
            {
                continue;
            }
        }
    }
}