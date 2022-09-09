#include "Diagnostics.h"
#include "Error.h"

#include <unordered_map>
#include <cstdio>
#include <stack>


namespace kickcat
{
    std::unordered_map<uint16_t, uint16_t> getTopology(std::vector<Slave>& slaves)
    {
        std::unordered_map<uint16_t, uint16_t> topology;

        uint16_t lastSeen = slaves.at(0).address;
        std::stack<uint16_t> branches;
        for (auto& slave : slaves) 
        {   
            int openPorts = slave.countOpenPorts();

            switch (openPorts)
            {   
                case 0:
                {
                    THROW_ERROR("No open port on a slave - it should not exist in the bus");
                    break;
                }
                case 1:
                {
                    topology[slave.address] = lastSeen;
                    lastSeen = slave.address;
                    if (not branches.empty())
                    {
                        lastSeen = branches.top();
                        branches.pop();
                    }
                    break;
                }
                case 2:
                {
                    topology[slave.address] = lastSeen;
                    lastSeen = slave.address;
                    break;
                }
                default:
                {
                    topology[slave.address] = lastSeen;
                    lastSeen = slave.address;
                    for (int i = 2; i < openPorts; ++i)
                    {
                        branches.push(slave.address);
                    }
                }
            }
        }
        return topology;
    }
}
