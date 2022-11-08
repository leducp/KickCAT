#ifndef KICKCAT_PRINTS_H
#define KICKCAT_PRINTS_H

#include <unordered_map>
#include <cstdint>

#include "Slave.h"
#include "protocol.h"


namespace kickcat
{
    // Slaves utils
    void printInfo(Slave const& slave);
    void printPDOs(Slave const& slave);
    void printErrorCounters(Slave const& slave);
    void printDLStatus(Slave const& slave);
    void printGeneralEntry(Slave const& slave);

    // Topology utils
    void printTopology(std::unordered_map<uint16_t, uint16_t> const& topology_mapping);
}

#endif
