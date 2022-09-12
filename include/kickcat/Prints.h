#ifndef KICKCAT_PRINTS_H
#define KICKCAT_PRINTS_H

#include <unordered_map>
#include <cstdint>

#include "Slave.h"


namespace kickcat
{
    // Slaves utils
    void printInfo(const Slave& slave);
    void printPDOs(const Slave& slave);
    void printErrorCounters(const Slave& slave);
    void printDLStatus(const Slave& slave);
    void printGeneralEntry(const Slave& slave);

    // Topology utils
    void printTopology(const std::unordered_map<uint16_t, uint16_t>& topology_mapping);
}

#endif
