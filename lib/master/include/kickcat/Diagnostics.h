#ifndef KICKCAT_DIAGNOSTICS_H
#define KICKCAT_DIAGNOSTICS_H

#include "kickcat/Slave.h"
#include <unordered_map>

namespace kickcat
{
    /// \brief return the topology of discovered network - To be called after bus.getDLStatus()
    /// \return [key, value] pair : [slave adress, parent address] (the only slave that is its own parent is linked to the master)
    std::unordered_map<uint16_t, uint16_t> getTopology(std::vector<Slave>& slaves);

    /// \brief Detect network topology using BFS traversal - To be called after DL status is fetched for all slaves
    /// \return Map of [slave address, parent address] pairs. The first slave has itself as parent (connected to master)
    std::unordered_map<uint16_t, uint16_t> detectTopology(std::vector<Slave>& slaves);
}

#endif
