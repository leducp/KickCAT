#ifndef KICKCAT_DIAGNOSTICS_H
#define KICKCAT_DIAGNOSTICS_H

#include "Slave.h"

#include <unordered_map>
#include <vector>

namespace kickcat
{
    /// \brief returns the topology of discovered network - To be called after bus.getDLStatus()
    /// \return [key, value] pair : [slave adress, parent address] (the only slave that is its own parent is linked to the master) 
    std::unordered_map<uint16_t, uint16_t> getTopology(std::vector<Slave>& slaves);
    
    /// \brief returns the topology of discovered network - To be called after bus.getDLStatus()
    /// \return [key, vector(values)] pair of connected slaves, whose position in the arrray correspond to the port of linkage
    std::unordered_map<uint16_t, std::vector<uint16_t>> completeTopologyMap(std::vector<Slave>& slaves);

    bool PortsAnalysis(std::vector<Slave>& slaves);
}

#endif
