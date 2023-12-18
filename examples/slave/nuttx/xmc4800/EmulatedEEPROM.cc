#include "EmulatedEEPROM.h"

#include "kickcat/checksum/adler32.h"
#include "kickcat/debug.h"

extern "C"
{
    #include <xmc4_flash.h>
}

#include <cstring>


namespace kickcat
{
    // XMC specific
    template<typename T>
    T* ecat_address(uint16_t offset)
    {
        void* address = reinterpret_cast<void*>(ECAT0_BASE_ADDRESS + offset);
        return static_cast<T*>(address);
    }

    constexpr uint32_t EEPROM_COMMAND_PENDING = 1 << 5;
    constexpr uint32_t EEPROM_LOADING_STATUS  = 1 << 12;

    void EmulatedEEPROM::init()
    {
        // Set eeprom to default value END category
        std::memset(eeprom_.data, 0xff, EEPROM_MAX_SIZE_BYTE);
        load_from_flash();

        eeprom::InfoEntry eeprom_desc;

        if (load_default_eeprom_)
        {
            createMinimalEEPROM(eeprom_desc);
            std::memset(eeprom_.data, 0xff, EEPROM_MAX_SIZE_BYTE);
            std::memcpy(eeprom_.data, &eeprom_desc, sizeof(eeprom::InfoEntry));
        }
        else
        {
            std::memcpy(&eeprom_desc, eeprom_.data, sizeof(eeprom_desc));
        }

        uint16_t control_status_reg = *ecat_address<uint16_t>(reg::EEPROM_CONTROL);

        // reset command
        control_status_reg &= ~eeprom::Control::COMMAND;
        std::memcpy(ecat_address<uint16_t>(reg::EEPROM_CONTROL), &control_status_reg, sizeof(control_status_reg));
    }


    void EmulatedEEPROM::process()
    {
        process_flash();
        uint32_t al_event_request = *ecat_address<uint32_t>(reg::AL_EVENT);

        uint16_t control_status_reg = *ecat_address<uint16_t>(reg::EEPROM_CONTROL);
        if ( not (al_event_request & EEPROM_COMMAND_PENDING))
        {
            // No command pending
            return;
        }

        uint32_t address = *ecat_address<uint32_t>(reg::EEPROM_ADDRESS);

        switch ((control_status_reg & eeprom::Control::COMMAND))
        {
            case eeprom::Control::READ:
            {
                constexpr uint32_t EEPROM_READ_BYTES = 8;
                std::memcpy(ecat_address<uint16_t>(reg::EEPROM_DATA), eeprom_.data + address, EEPROM_READ_BYTES);

                // ACK command, error bit cleared by writing command, clear CRC errors, all other bits read only
                eeprom::Control read = eeprom::Control::READ;
                std::memcpy(ecat_address<uint16_t>(reg::EEPROM_CONTROL), &read, sizeof(control_status_reg));
                break;
            }

            case eeprom::Control::WRITE:
            {
                std::memcpy(eeprom_.data + address, ecat_address<uint16_t>(reg::EEPROM_DATA), 2);

                // ACK command, error bit cleared by writing command, clear CRC errors, all other bits read only
                eeprom::Control write = eeprom::Control::WRITE;
                std::memcpy(ecat_address<uint16_t>(reg::EEPROM_CONTROL), &write, sizeof(control_status_reg));

                shall_write_ = true;
                last_write_ = duration_cast<milliseconds>(since_epoch());
                break;
            }

            case eeprom::Control::RELOAD:
            {
                eeprom::InfoEntry eeprom_desc;
                std::memcpy(&eeprom_desc, eeprom_.data, sizeof(eeprom::InfoEntry));

                // INFINEON way of implementing the reload, seems to differ from EtherCAT register description section II,
                // Table 67: Register EEPROM Data for EEPROM Emulation Reload IP Core (0x0508:0x050F)
                uint32_t data_to_load = eeprom_desc.pdi_control + (eeprom_desc.pdi_configuration << 16);
                std::memcpy(ecat_address<uint32_t>(reg::EEPROM_DATA), &data_to_load, sizeof(data_to_load));
                data_to_load = eeprom_desc.sync_impulse_length + (eeprom_desc.pdi_configuration_2 << 16);
                std::memcpy(ecat_address<uint32_t>(reg::EEPROM_DATA + 4), &data_to_load, sizeof(data_to_load));
                eeprom::Control reload = eeprom::Control::RELOAD;
                std::memcpy(ecat_address<uint16_t>(reg::EEPROM_CONTROL), &reload, sizeof(eeprom::Control));

                data_to_load = eeprom_desc.station_alias + (eeprom_desc.reserved1 << 16);
                std::memcpy(ecat_address<uint32_t>(reg::EEPROM_DATA), &data_to_load, sizeof(data_to_load));
                data_to_load = eeprom_desc.reserved2 + (eeprom_desc.crc << 16);
                std::memcpy(ecat_address<uint32_t>(reg::EEPROM_DATA + 4), &data_to_load, sizeof(data_to_load));
                std::memcpy(ecat_address<uint16_t>(reg::EEPROM_CONTROL), &reload, sizeof(eeprom::Control));

                // wait until eeprom is loaded.
                while (*ecat_address<uint32_t>(reg::AL_EVENT) & EEPROM_LOADING_STATUS);

                slave_info("eeprom RELOAD command\n");
                break;
            }
            case eeprom::Control::NOP:
            {
                break;
            }
            default:
            {
                slave_info("Invalid eeprom command %x \n", control_status_reg);
            }
        }
    }

    void EmulatedEEPROM::load_from_flash()
    {
        std::memcpy(&eeprom_, (uint16_t*) EEPROM_FLASH_ADR, sizeof(eeprom_));

        uint32_t adler_sum = adler32Sum(eeprom_.data, EEPROM_MAX_SIZE_BYTE);

        slave_info("Adler sum read %lx, computed %lx\n", eeprom_.adler_checksum, adler_sum);

        if (adler_sum != eeprom_.adler_checksum)
        {
            slave_info("Checksum Mismatch !! Load default eeprom \n");
            load_default_eeprom_ = true;
        }

        // debug print:
        for(uint32_t i = 0; i < 20; i++)
        {
            slave_info("EEPROM %li : %x\n", i, eeprom_.data[i]);
        }
    }


    void EmulatedEEPROM::process_flash()
    {
        if (not shall_write_ or elapsed_time(last_write_) < EEPROM_WRITE_TIMEOUT)
        {
            // There is no need to write to flash.
            return;
        }

        shall_write_ = false;
        slave_info("Erase previous flash \n");
        xmc4_flash_erase_sector(EEPROM_FLASH_ADR);
        slave_info("Start write flash \n");

        eeprom_.adler_checksum = adler32Sum(eeprom_.data, EEPROM_MAX_SIZE_BYTE);

        uint32_t const* eeprom_base = reinterpret_cast<uint32_t const*>(&eeprom_);
        for (uint32_t p = 0; p < PAGE_NUMBER; p++)
        {
            xmc4_flash_clear_status();
            xmc4_flash_enter_page_mode();

            for (uint32_t i = 0; i < PAGE_SIZE_UINT64; i += 2)
            {
                uint32_t index = p * PAGE_SIZE_UINT64 + i;
                uint32_t const* low_word  = eeprom_base + index;
                uint32_t const* high_word = eeprom_base + index + 1;
                xmc4_flash_load_page(*low_word, *high_word);
            }
            xmc4_flash_write_page(EEPROM_FLASH_ADR + p * PAGE_SIZE_BYTE);

            // wait write finish
            while(*(uint32_t*)(XMC4_FLASH_FSR) & 0x1);
        }
        slave_info("End write flash \n");
    }
}
