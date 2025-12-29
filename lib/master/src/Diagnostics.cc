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


    std::unordered_map<uint16_t, uint16_t> detectTopology(std::vector<Slave>& slaves)
    {
        std::unordered_map<uint16_t, uint16_t> topology;

        if (slaves.empty())
        {
            return topology;
        }

        // Validate all slaves have at least one open port
        for (auto& slave : slaves)
        {
            if (slave.countOpenPorts() == 0)
            {
                THROW_ERROR("No open port on slave - it should not exist in the bus");
            }
        }

        // BFS-based topology detection
        // Use a queue to process slaves level by level, building parent-child relationships
        std::queue<size_t> bfs_queue;
        std::unordered_set<uint16_t> processed;
        
        // Track how many connection slots each slave has available
        // A connection slot represents a port that can connect to a child
        std::vector<int> available_slots(slaves.size(), 0);
        
        // Initialize: first slave is connected to master
        size_t first_idx = 0;
        topology[slaves[first_idx].address] = slaves[first_idx].address;
        processed.insert(slaves[first_idx].address);
        
        // First slave: one port to master, remaining ports available for children
        int first_ports = slaves[first_idx].countOpenPorts();
        available_slots[first_idx] = first_ports - 1;
        
        if (available_slots[first_idx] > 0)
        {
            bfs_queue.push(first_idx);
        }

        // Process slaves in order using BFS
        size_t slave_idx = 1;
        
        while (!bfs_queue.empty() && slave_idx < slaves.size())
        {
            size_t parent_idx = bfs_queue.front();
            bfs_queue.pop();
            
            // Connect children to this parent while it has available slots
            while (available_slots[parent_idx] > 0 && slave_idx < slaves.size())
            {
                size_t child_idx = slave_idx++;
                auto& child = slaves[child_idx];
                
                // Set parent-child relationship
                topology[child.address] = slaves[parent_idx].address;
                processed.insert(child.address);
                
                // Decrease available slots for parent (one port used for this child)
                available_slots[parent_idx]--;
                
                // Calculate available slots for child
                // Child has one port connected to parent, remaining ports available for its children
                int child_ports = child.countOpenPorts();
                available_slots[child_idx] = child_ports - 1;
                
                // If child can have children, add it to queue for BFS processing
                if (available_slots[child_idx] > 0)
                {
                    bfs_queue.push(child_idx);
                }
            }
        }

        // If there are remaining unprocessed slaves, they should be connected to available parents
        // This handles cases where we have branch points with multiple children
        while (slave_idx < slaves.size())
        {
            // Find a parent with available slots
            bool found_parent = false;
            for (size_t i = 0; i < slaves.size() && slave_idx < slaves.size(); ++i)
            {
                if (available_slots[i] > 0 && processed.find(slaves[i].address) != processed.end())
                {
                    size_t child_idx = slave_idx++;
                    topology[slaves[child_idx].address] = slaves[i].address;
                    processed.insert(slaves[child_idx].address);
                    available_slots[i]--;
                    
                    int child_ports = slaves[child_idx].countOpenPorts();
                    available_slots[child_idx] = child_ports - 1;
                    
                    if (available_slots[child_idx] > 0)
                    {
                        bfs_queue.push(child_idx);
                    }
                    found_parent = true;
                    break;
                }
            }
            
            if (!found_parent)
            {
                // Should not happen in valid topology, but handle gracefully
                break;
            }
        }

        return topology;
    }
}
