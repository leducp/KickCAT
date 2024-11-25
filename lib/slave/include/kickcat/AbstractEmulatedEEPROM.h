#ifndef KICKCAT_ABSTRACT_EMULATED_EEPROM
#define KICKCAT_ABSTRACT_EMULATED_EEPROM

#include "kickcat/protocol.h"
#include "kickcat/OS/Time.h"

#include <vector>

namespace kickcat
{

constexpr uint32_t EEPROM_COMMAND_PENDING = 1 << 5;
constexpr uint32_t EEPROM_LOADING_STATUS  = 1 << 12;

constexpr milliseconds EEPROM_WRITE_TIMEOUT = 400ms;


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

class AbstractEmulatedEEPROM
{
public:
    void init();
    void process();

private:
    void loadFromFlash();
    void processFlash();

    /// \brief Populate content of eeprom_ from flash memory.
    ///
    /// \details Expect to read first the addler32 sum, then
    /// the eeprom content.
    virtual void readFlash() = 0;

    /// \brief Dump content of eeprom_ into flash memory.
    ///
    /// \details It shall store the addler32 sum computed on
    /// the eeprom content and then the eeprom content.
    virtual void writeFlash() = 0;

    template<typename T>
    T* ecatAddress(uint16_t offset);

    milliseconds last_write_{0ms};
    bool load_default_eeprom_{false};

protected:
    uint32_t adler_checksum_;
    std::vector<uint16_t> eeprom_; // Needs to be init by daughter class.
    bool shall_write_{false};

    // Chip dependant attributes:
    uint32_t ecat_base_address_;

};
}
#endif
