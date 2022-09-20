#include <unordered_map>
#include "Slave.h"
#include <cstdint>
#include <vector>

namespace kickcat
{
    /// \brief return the topology of discovered network - To be called after bus.getDLStatus()
    /// \return [key, value] pair : [slave adress, parent address] (the only slave that is its own parent is linked to the master) 
    std::unordered_map<uint16_t, uint16_t> getTopology(std::vector<Slave>& slaves);
}