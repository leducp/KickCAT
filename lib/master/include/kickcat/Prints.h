#ifndef KICKCAT_PRINTS_H
#define KICKCAT_PRINTS_H

#include <unordered_map>
#include <cstdint>

#include "Slave.h"
#include "kickcat/protocol.h"


namespace kickcat
{
    // Slaves utils
    void printInfo(Slave const& slave);
    void printPDOs(Slave const& slave);
    void printESC(Slave const& slave);

    // Topology utils
    void print(std::unordered_map<uint16_t, uint16_t> const& topology_mapping);

    // Helpers to parse register and get human readable output from them
    char const* fmmuTypeToString(uint8_t fmmu_type);
    char const* typeToString(uint8_t esc_type);
    char const* portToString(uint8_t esc_port_desc);
    std::string featuresToString(uint16_t esc_features);
}

#endif
