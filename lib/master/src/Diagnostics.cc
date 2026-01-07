#include "Diagnostics.h"
#include "Error.h"

#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <cstdio>
#include <stack>


namespace kickcat
{
    std::unordered_map<uint16_t, uint16_t> getTopology(std::vector<Slave>& slaves)
    {
        std::unordered_map<uint16_t, uint16_t> topology;

        uint16_t last_seen = slaves.at(0).address;
        std::stack<uint16_t> branches;
        for (auto& slave : slaves)
        {
            int open_ports = slave.countOpenPorts();

            switch (open_ports)
            {
                case 0:
                {
                    THROW_ERROR("No open port on a slave - it should not exist in the bus");
                    break;
                }
                case 1:
                {
                    topology[slave.address] = last_seen;
                    last_seen = slave.address;
                    if (not branches.empty())
                    {
                        last_seen = branches.top();
                        branches.pop();
                    }
                    break;
                }
                case 2:
                {
                    topology[slave.address] = last_seen;
                    last_seen = slave.address;
                    break;
                }
                default:
                {
                    topology[slave.address] = last_seen;
                    last_seen = slave.address;
                    for (int i = 2; i < open_ports; ++i)
                    {
                        branches.push(slave.address);
                    }
                }
            }
        }
        return topology;
    }
}
