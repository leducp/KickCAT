#ifndef KICKCAT_UTILS_XSD_H
#define KICKCAT_UTILS_XSD_H

#include <cstdint>
#include <string_view>
#include <vector>

// Values of the XML schema types shared by ESI (ETG.2000) and ENI (ETG.2100) files.
// Errors throw std::invalid_argument without context: callers add the element they were reading.
namespace kickcat::xsd
{
    /// Strip the XML whitespace (space, tab, CR, LF) around text.
    std::string_view trim(std::string_view text);

    /// HexDecValue: [+-]?[0-9]+ or #x (0x accepted) followed by hex digits, surrounding spaces ignored.
    /// Unsigned values up to 2^64 - 1 keep their bit pattern.
    int64_t parseHexDec(std::string_view text);

    /// xs:boolean: 1, 0, true, false.
    bool parseBoolean(std::string_view text);

    /// xs:hexBinary, in wire order; embedded whitespace (wrapped lines) ignored.
    std::vector<uint8_t> parseHexBinary(std::string_view text);
}

#endif
