#ifndef KICKCAT_XMC4800_EEPROM
#define KICKCAT_XMC4800_EEPROM

#include "kickcat/AbstractEmulatedEEPROM.h"

namespace kickcat
{
    constexpr uint32_t EEPROM_CHECKSUM_SIZE = 8;
    constexpr uint32_t PAGE_SIZE_BYTE = 256;
    constexpr uint32_t PAGE_SIZE_UINT32 = (PAGE_SIZE_BYTE / 4);
    constexpr uint32_t PAGE_SIZE_UINT64 = (PAGE_SIZE_UINT32 / 2);
    constexpr uint32_t PAGE_NUMBER = 16;
    constexpr uint32_t EEPROM_MAX_SIZE_BYTE = (PAGE_NUMBER * PAGE_SIZE_BYTE - EEPROM_CHECKSUM_SIZE);
    constexpr uint32_t EEPROM_MAX_SIZE_WORD = (EEPROM_MAX_SIZE_BYTE / 2);

    class XMC4800EEPROM : public AbstractEmulatedEEPROM
    {
    public:
        XMC4800EEPROM();
        ~XMC4800EEPROM() = default;

    private:
        void readFlash() override;
        void writeFlash() override;

        /// \brief Flash needs to be written aligned with page size.
        ///
        /// \param data must be a multiple of 64 bits.
        /// \param size size in bytes.
        uint32_t write(void* data, uint32_t size);
        void writeLastPage();

        uint32_t page_cursor_ = 0; // cursor jumps from 64 bits in the page.
        uint32_t page_index_ = 0;
    };
}
#endif
