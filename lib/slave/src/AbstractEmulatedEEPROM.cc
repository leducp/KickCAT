#include "AbstractEmulatedEEPROM.h"

#include "kickcat/checksum/adler32.h"
#include "kickcat/debug.h"

#include "kickcat/EEPROM/EEPROM_factory.h"

#include <cstring>

namespace kickcat
{
    // XMC specific
    template<typename T>
    T* AbstractEmulatedEEPROM::ecatAddress(uint16_t offset)
    {
        void* address = reinterpret_cast<void*>(ecat_base_address_ + offset);
        return static_cast<T*>(address);
    }


    void AbstractEmulatedEEPROM::init()
    {
        // Set eeprom to default value END category
        eeprom_.assign(eeprom_.size(), 0xffff);
        loadFromFlash();

        eeprom::InfoEntry eeprom_desc;

        if (load_default_eeprom_)
        {
            eeprom_.assign(eeprom_.size(), 0xffff);
            createMinimalEEPROM(eeprom_desc);
            std::memcpy(eeprom_.data(), &eeprom_desc, sizeof(eeprom::InfoEntry));
        }
        else
        {
            std::memcpy(&eeprom_desc, eeprom_.data(), sizeof(eeprom_desc));
        }

        uint16_t control_status_reg = *ecatAddress<uint16_t>(reg::EEPROM_CONTROL);

        // reset command
        control_status_reg &= ~eeprom::Control::COMMAND;
        std::memcpy(ecatAddress<uint16_t>(reg::EEPROM_CONTROL), &control_status_reg, sizeof(control_status_reg));
    }


    void AbstractEmulatedEEPROM::process()
    {
        processFlash();
        uint32_t al_event_request = *ecatAddress<uint32_t>(reg::AL_EVENT);

        uint16_t control_status_reg = *ecatAddress<uint16_t>(reg::EEPROM_CONTROL);
        if ( not (al_event_request & EEPROM_COMMAND_PENDING))
        {
            // No command pending
            return;
        }

        uint32_t address = *ecatAddress<uint32_t>(reg::EEPROM_ADDRESS);

        switch ((control_status_reg & eeprom::Control::COMMAND))
        {
            case eeprom::Control::READ:
            {
                constexpr uint32_t EEPROM_READ_BYTES = 8;
                std::memcpy(ecatAddress<uint16_t>(reg::EEPROM_DATA), eeprom_.data() + address, EEPROM_READ_BYTES);

                // ACK command, error bit cleared by writing command, clear CRC errors, all other bits read only
                eeprom::Control read = eeprom::Control::READ;
                std::memcpy(ecatAddress<uint16_t>(reg::EEPROM_CONTROL), &read, sizeof(control_status_reg));
                break;
            }

            case eeprom::Control::WRITE:
            {
                std::memcpy(eeprom_.data() + address, ecatAddress<uint16_t>(reg::EEPROM_DATA), 2);

                // ACK command, error bit cleared by writing command, clear CRC errors, all other bits read only
                eeprom::Control write = eeprom::Control::WRITE;
                std::memcpy(ecatAddress<uint16_t>(reg::EEPROM_CONTROL), &write, sizeof(control_status_reg));

                shall_write_ = true;
                last_write_ = duration_cast<milliseconds>(since_epoch());
                break;
            }

            case eeprom::Control::RELOAD:
            {
                eeprom::InfoEntry eeprom_desc;
                std::memcpy(&eeprom_desc, eeprom_.data(), sizeof(eeprom::InfoEntry));

                // INFINEON way of implementing the reload, seems to differ from EtherCAT register description section II,
                // Table 67: Register EEPROM Data for EEPROM Emulation Reload IP Core (0x0508:0x050F)
                uint32_t data_to_load = eeprom_desc.pdi_control + (eeprom_desc.pdi_configuration << 16);
                std::memcpy(ecatAddress<uint32_t>(reg::EEPROM_DATA), &data_to_load, sizeof(data_to_load));
                data_to_load = eeprom_desc.sync_impulse_length + (eeprom_desc.pdi_configuration_2 << 16);
                std::memcpy(ecatAddress<uint32_t>(reg::EEPROM_DATA + 4), &data_to_load, sizeof(data_to_load));
                eeprom::Control reload = eeprom::Control::RELOAD;
                std::memcpy(ecatAddress<uint16_t>(reg::EEPROM_CONTROL), &reload, sizeof(eeprom::Control));

                data_to_load = eeprom_desc.station_alias + (eeprom_desc.reserved1 << 16);
                std::memcpy(ecatAddress<uint32_t>(reg::EEPROM_DATA), &data_to_load, sizeof(data_to_load));
                data_to_load = eeprom_desc.reserved2 + (eeprom_desc.crc << 16);
                std::memcpy(ecatAddress<uint32_t>(reg::EEPROM_DATA + 4), &data_to_load, sizeof(data_to_load));
                std::memcpy(ecatAddress<uint16_t>(reg::EEPROM_CONTROL), &reload, sizeof(eeprom::Control));

                // wait until eeprom is loaded.
                while (*ecatAddress<uint32_t>(reg::AL_EVENT) & EEPROM_LOADING_STATUS);

                printf("eeprom RELOAD command\n");
                break;
            }
            case eeprom::Control::NOP:
            {
                break;
            }
            default:
            {
                printf("Invalid eeprom command %x \n", control_status_reg);
            }
        }
    }

    void AbstractEmulatedEEPROM::loadFromFlash()
    {
        readFlash();

        uint32_t adler_sum = adler32Sum(eeprom_.data(), eeprom_.size() * 2);

        printf("Adler sum read %lx, computed %lx\n", adler_checksum_, adler_sum);

        if (adler_sum != adler_checksum_)
        {
            printf("Checksum Mismatch !! Load default eeprom \n");
            load_default_eeprom_ = true;
        }

        // debug print:
        for(uint32_t i = 0; i < 20; i++)
        {
            printf("EEPROM %li : %x\n", i, eeprom_[i]);
        }
    }


    void AbstractEmulatedEEPROM::processFlash()
    {
        if (not shall_write_ or elapsed_time(last_write_) < EEPROM_WRITE_TIMEOUT)
        {
            // There is no need to write to flash.
            return;
        }

        writeFlash();
    }
}
