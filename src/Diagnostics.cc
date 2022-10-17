#include "Diagnostics.h"
#include "Error.h"

#include <unordered_map>
#include <cstdio>
#include <stack>
#include <algorithm>


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

    std::unordered_map<uint16_t, std::vector<uint16_t>> completeTopologyMap(std::vector<Slave>& slaves)
    {
        std::unordered_map<uint16_t, std::vector<uint16_t>> completeTopology;

        std::unordered_map<uint16_t, uint16_t> topology = getTopology(slaves);
        std::unordered_map<uint16_t, std::vector<int>> slaves_ports;
        std::unordered_map<uint16_t, std::vector<uint16_t>> links;

        for (auto& slave : slaves)
        {
            std::vector<int> ports;
            switch (slave.countOpenPorts())
            {
                case 1:
                {
                    ports.push_back(0);
                    break;
                }
                case 2:
                {
                    ports.push_back(0);
                    ports.push_back(1);
                    break;
                }
                case 3:
                {
                    ports.push_back(0);
                    if (slave.dl_status.PL_port3) {ports.push_back(3);};
                    if (slave.dl_status.PL_port1) {ports.push_back(1);};
                    if (slave.dl_status.PL_port2) {ports.push_back(2);};
                    break;
                }
                case 4:
                {
                    ports.push_back(0);
                    ports.push_back(3);
                    ports.push_back(1);
                    ports.push_back(2);
                    break;
                }
                default:
                {
                    THROW_ERROR("Error in ports order\n");
                }
            }
            slaves_ports[slave.address] = ports;

            links[topology[slave.address]].push_back(slave.address);
            if (slave.address != topology[slave.address]) {links[slave.address].push_back(topology[slave.address]);}
        }

        for (auto& slave : slaves)
        {
            completeTopology[slave.address] = {0, 0, 0, 0};
            std::sort(links[slave.address].begin(), links[slave.address].end());
            for (size_t i = 0; i < links[slave.address].size(); ++i)
            {
                completeTopology[slave.address].at(slaves_ports[slave.address][i]) = links[slave.address].at(i); 
            }
        }

        return completeTopology;
    }

    bool PortsAnalysis(std::vector<Slave>& slaves)
    {
        std::unordered_map<uint16_t, std::vector<uint16_t>> completeTopology = completeTopologyMap(slaves);
        bool check = true;

        printf("\nPHY Layer Errors/Invalid Frames/Forwarded/Lost Links\n");
        printf("  Slave  ");
        for (uint16_t slave_id = 0; slave_id < (uint16_t) slaves.size(); ++slave_id) {printf(" %04x ", slave_id);}
        printf("|");
        for (uint16_t slave_id = 0; slave_id < (uint16_t) slaves.size(); ++slave_id) {printf(" %04x ", slave_id);}
        printf("|");
        for (uint16_t slave_id = 0; slave_id < (uint16_t) slaves.size(); ++slave_id) {printf(" %04x ", slave_id);}
        printf("|");
        for (uint16_t slave_id = 0; slave_id < (uint16_t) slaves.size(); ++slave_id) {printf(" %04x ", slave_id);}

        printf("\n");
        for (auto port = 0; port < 4; ++port)
        {
            printf("Port %i : ", port);

            //PHY
            for (uint16_t slave_id = 0; slave_id < (uint16_t) slaves.size(); ++slave_id)
            {
                if (slaves.at(slave_id).error_counters.rx[port].physical_layer > 0) {check = false;}
                printf("  %03d ", slaves.at(slave_id).error_counters.rx[port].physical_layer);
            }
            printf("|");

            //Invalid
            for (uint16_t slave_id = 0; slave_id < (uint16_t) slaves.size(); ++slave_id)
            {
                if (slaves.at(slave_id).error_counters.rx[port].invalid_frame > 0) {check = false;}
                printf("  %03d ", slaves.at(slave_id).error_counters.rx[port].invalid_frame);
            }
            printf("|");

            //Forwarded
            for (uint16_t slave_id = 0; slave_id < (uint16_t) slaves.size(); ++slave_id)
            {
                if (slaves.at(slave_id).error_counters.forwarded[port] > 0) {check = false;}
                printf("  %03d ", slaves.at(slave_id).error_counters.forwarded[port]);
            }
            printf("|");

            for (uint16_t slave_id = 0; slave_id < (uint16_t) slaves.size(); ++slave_id)
            {
                if (slaves.at(slave_id).error_counters.lost_link[port] > 0) {check = false;}
                printf("  %03d ", slaves.at(slave_id).error_counters.lost_link[port]);
            }
            printf("\n");
        }

        return check;
    }
}
