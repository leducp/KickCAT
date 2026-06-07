#ifndef KICKCAT_ESI_SIIBUILDER_H
#define KICKCAT_ESI_SIIBUILDER_H

#include <cstdint>
#include <vector>

#include "kickcat/ESI/Device.h"
#include "kickcat/SIIParser.h"

namespace kickcat::ESI
{
    // Compile a parsed ESI device into an SII structure (ETG.2010): identity,
    // strings, general, sync managers, FMMUs, Tx/Rx PDO mappings, and vendor
    // categories. The DC category and image/order-code strings are not synthesized.
    eeprom::SII buildSII(Device const& device);

    // buildSII(device).serialize(), except a raw <Eeprom><Data> image is returned
    // verbatim when the ESI shipped one.
    std::vector<uint8_t> buildEepromImage(Device const& device);
}

#endif
