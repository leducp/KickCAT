#include "kickcat/EEPROM/XMC4800EEPROM.h"

#include "kickcat/ESC/XMC4800.h"
#include "kickcat/checksum/adler32.h"
#include "kickcat/debug.h"

extern "C"
{
    #include <xmc4_flash.h>
    #include "hardware/xmc4_flash.h"
}

#include <cstring>

namespace kickcat
{
    constexpr uint32_t EEPROM_FLASH_ADR = XMC4_FLASH_S15;

    XMC4800EEPROM::XMC4800EEPROM()
    {
        ecat_base_address_ = ECAT0_BASE_ADDRESS;
        eeprom_.resize(EEPROM_MAX_SIZE_WORD);
    }


    void XMC4800EEPROM::readFlash()
    {
        std::memcpy(&adler_checksum_, reinterpret_cast<void*>(EEPROM_FLASH_ADR), sizeof(adler_checksum_));
        std::memcpy(eeprom_.data(), reinterpret_cast<void*>(EEPROM_FLASH_ADR + sizeof(uint64_t)), EEPROM_MAX_SIZE_BYTE);
    }


    void XMC4800EEPROM::writeFlash()
    {
        shall_write_ = false;
        slave_info("Erase previous flash \n");
        xmc4_flash_erase_sector(EEPROM_FLASH_ADR);
        slave_info("Start write flash \n");

        adler_checksum_ = adler32Sum(eeprom_.data(), EEPROM_MAX_SIZE_BYTE);

        xmc4_flash_clear_status();
        xmc4_flash_enter_page_mode();

        write(&adler_checksum_, sizeof(uint64_t)); // uint64_t for padding.

        uint32_t written_byte = 0;
        while (written_byte / 2 < eeprom_.size())
        {
            written_byte += write(&eeprom_[written_byte/2], eeprom_.size() * 2 - written_byte);
        }

        writeLastPage();

        slave_info("End write flash \n");
    }

    uint32_t XMC4800EEPROM::write(void* data, uint32_t size_byte)
    {

        uint32_t* data_ref = reinterpret_cast<uint32_t*>(data);
        uint32_t size_uint64 = size_byte / 8;

        uint32_t remaining = PAGE_SIZE_UINT64 - page_cursor_;


        uint32_t written = 0;

        if (size_uint64 < remaining)
        {
            for (uint32_t i = 0; i < size_uint64 * 2; i+=2)
            {
                page_cursor_++;
                uint32_t const* low_word  = data_ref + i;
                uint32_t const* high_word = data_ref + i + 1;
                xmc4_flash_load_page(*low_word, *high_word);
            }
            written = size_byte;
        }
        else
        {
            for (uint32_t i = 0; i < remaining * 2; i+=2)
            {
                uint32_t const* low_word  = data_ref + i;
                uint32_t const* high_word = data_ref + i + 1;
                xmc4_flash_load_page(*low_word, *high_word);
            }
            written+= remaining * 8;
            page_cursor_ = 0;

            xmc4_flash_write_page(EEPROM_FLASH_ADR + page_index_ * PAGE_SIZE_BYTE);
            page_index_++;
            // wait write finish
            while(*(uint32_t*)(XMC4_FLASH_FSR) & 0x1);

            xmc4_flash_clear_status();
            xmc4_flash_enter_page_mode();
        }

        return written;
    }


    void XMC4800EEPROM::writeLastPage()
    {
        // Add padding in case of imcomplete page.
        if (page_cursor_ != 0)
        {
            uint32_t remaining = PAGE_SIZE_UINT64 - page_cursor_;

            for (uint32_t i = 0; i < remaining; i++)
            {
                xmc4_flash_load_page(0xff, 0xff); // End of eeprom symbol.
            }
            page_cursor_ = 0;
            xmc4_flash_write_page(EEPROM_FLASH_ADR + page_index_ * PAGE_SIZE_BYTE);
            page_index_++;
            // wait write finish
            while(*(uint32_t*)(XMC4_FLASH_FSR) & 0x1);
        }
    }
}
