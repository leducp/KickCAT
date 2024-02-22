#ifndef KICKCAT_EEPROM_FACTORY
#define KICKCAT_EEPROM_FACTORY

#include "kickcat/protocol.h"


/// This file is dedicated to init default values when EEPROM is missing/corrupted.

namespace kickcat
{
    // Default description, serves as an initial EEPROM which allow to start
    // the ESC and use a master to write a custom EEPROM.
    void createMinimalEEPROM(eeprom::InfoEntry& eeprom);
}
#endif
