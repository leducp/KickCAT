#ifndef KICKCAT_PRINTS_H
#define KICKCAT_PRINTS_H

#include <unordered_map>
#include <cstdint>

#include "Slave.h"


namespace kickcat
{
    // Slaves utils
    void printInfo(Slave slave);
    void printPDOs(Slave slave);
    void printErrorCounters(Slave slave);
    void printDLStatus(Slave slave);
    void printGeneralEntry(Slave slave);

    // Topology utils
    void printTopology(std::unordered_map<uint16_t, uint16_t> map);
}

#endif
