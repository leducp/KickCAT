#ifndef KICKCAT_EMULATED_EEPROM
#define KICKCAT_EMULATED_EEPROM

#include "kickcat/protocol.h"
#include "kickcat/Time.h"
#include "kickcat/ESC/XMC4800.h"
#include "EEPROM_factory.h"

#include "hardware/xmc4_flash.h"

namespace kickcat
{

constexpr uint32_t EEPROM_CHECKSUM_SIZE = 4;
constexpr uint32_t PAGE_SIZE_BYTE = 256;
constexpr uint32_t PAGE_SIZE_UINT64 = (PAGE_SIZE_BYTE / 4);
constexpr uint32_t PAGE_NUMBER = 16;
constexpr uint32_t EEPROM_MAX_SIZE_BYTE = (PAGE_NUMBER * PAGE_SIZE_BYTE - EEPROM_CHECKSUM_SIZE);
constexpr uint32_t EEPROM_MAX_SIZE_WORD = (EEPROM_MAX_SIZE_BYTE / 2);

struct EEPROM
{
    uint32_t adler_checksum;
    uint16_t data[EEPROM_MAX_SIZE_WORD];
}__attribute__((packed, aligned(4)));

constexpr uint32_t EEPROM_FLASH_ADR = XMC4_FLASH_S15;

constexpr milliseconds EEPROM_WRITE_TIMEOUT = 100ms;


/// \brief Emulate the EEPROM accessed by the ESC.
///
/// \details In EtherCAT the ESC either access to a physical EEPROM or the
/// the microcontroller emulate the EEPROM. The XMC 4800 needs to emulate it.
/// The EEPROM protocol is implemented and respond to ESC request in the dedicated
/// register. For persistency, the EEPROM is written on the flash of the board.
/// Implementation of the flash writing is naive and use the same portion of the same
/// sector, it assumes the EEPROM will not often be modified by the user.
/// An adler checksum is used to verify the eeprom has not been corrupted.
/// In case of corrupted or absent EEPROM, a default version is loaded, allowing to start
/// the ESC and flash a valid EEPROM.

class EmulatedEEPROM
{
public:
    EmulatedEEPROM() = default;
    void init();
    void process();

private:
    void load_from_flash();
    void process_flash();

    EEPROM eeprom_;
    milliseconds last_write_{0ms};
    bool shall_write_{false};
    bool load_default_eeprom_{false};
};
}
#endif
