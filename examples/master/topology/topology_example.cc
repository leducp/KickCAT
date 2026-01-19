#include <iostream>
#include <iomanip>

#include "kickcat/Link.h"
#include "kickcat/Bus.h"
#include "kickcat/Diagnostics.h"
#include "kickcat/Prints.h"
#include "kickcat/helpers.h"

using namespace kickcat;

void printTopology(const std::unordered_map<uint16_t, uint16_t>& topology, std::vector<Slave>& slaves)
{
    std::cout << "\n=== Network Topology ===\n";
    std::cout << std::left << std::setw(15) << "Slave Address" 
              << std::setw(15) << "Parent Address" 
              << std::setw(20) << "Open Ports" 
              << "Description\n";
    std::cout << std::string(80, '-') << "\n";

    for (auto& slave : slaves)
    {
        uint16_t parent = topology.at(slave.address);
        int open_ports = slave.countOpenPorts();
        
        std::string description;
        if (slave.address == parent)
        {
            description = "Connected to master";
        }
        else
        {
            description = "Child of slave " + std::to_string(parent);
        }

        std::cout << std::left << std::setw(15) << slave.address
                  << std::setw(15) << parent
                  << std::setw(20) << open_ports
                  << description << "\n";
    }
    std::cout << "\n";
}

int main(int argc, char* argv[])
{
    if (argc != 3 and argc != 2)
    {
        printf("Usage with redundancy: %s NIC_nominal NIC_redundancy\n", argv[0]);
        printf("Usage without redundancy: %s NIC_nominal\n", argv[0]);
        return 1;
    }

    std::string red_interface_name = "";
    std::string nom_interface_name = argv[1];
    if (argc == 3)
    {
        red_interface_name = argv[2];
    }

    std::shared_ptr<AbstractSocket> socket_nominal;
    std::shared_ptr<AbstractSocket> socket_redundancy;
    try
    {
        auto [nominal, redundancy] = createSockets(nom_interface_name, red_interface_name);
        socket_nominal = nominal;
        socket_redundancy = redundancy;
    }
    catch (std::exception const& e)
    {
        std::cerr << "Error creating sockets: " << e.what() << std::endl;
        return 1;
    }

    auto report_redundancy = []()
    {
        printf("Redundancy has been activated due to loss of a cable\n");
    };

    std::shared_ptr<Link> link = std::make_shared<Link>(socket_nominal, socket_redundancy, report_redundancy);
    link->setTimeout(2ms);
    link->checkRedundancyNeeded();

    Bus bus(link);

    try
    {
        std::cout << "Initializing EtherCAT bus...\n";
        bus.init(100ms);
        std::cout << "Bus initialized successfully. " << bus.detectedSlaves() << " slave(s) detected.\n\n";

        // Fetch DL status for all slaves (required before topology detection)
        std::cout << "Fetching DL status for all slaves...\n";
        auto error_callback = [](DatagramState const& state)
        {
            THROW_ERROR_DATAGRAM("Error fetching DL status", state);
        };

        for (auto& slave : bus.slaves())
        {
            bus.sendGetDLStatus(slave, error_callback);
        }
        bus.processAwaitingFrames();
        std::cout << "DL status fetched successfully.\n\n";

        // Display DL status for each slave
        std::cout << "=== DL Status for each slave ===\n";
        for (auto& slave : bus.slaves())
        {
            std::cout << "Slave " << slave.address << ":\n";
            std::cout << toString(slave.dl_status);
            std::cout << "Open ports: " << slave.countOpenPorts() << "\n\n";
        }

        // Compare with the original stack-based function
        std::cout << "Detecting topology\n";
        auto topology = getTopology(bus.slaves());
        printTopology(topology, bus.slaves());

        // Display topology as a tree structure
        std::cout << "=== Topology Tree Structure ===\n";
        std::unordered_map<uint16_t, std::vector<uint16_t>> children_map;
        uint16_t root = 0;
        
        for (const auto& [slave_addr, parent_addr] : topology)
        {
            if (slave_addr == parent_addr)
            {
                root = slave_addr;
            }
            else
            {
                children_map[parent_addr].push_back(slave_addr);
            }
        }

        std::function<void(uint16_t, int)> printTree = [&](uint16_t node, int depth)
        {
            std::string indent(depth * 2, ' ');
            std::cout << indent;
            if (depth > 0)
            {
                std::cout << "└─ ";
            }
            std::cout << "Slave " << node;
            if (node == root)
            {
                std::cout << " (Root)";
            }
            std::cout << "\n";

            if (children_map.find(node) != children_map.end())
            {
                for (auto child : children_map[node])
                {
                    printTree(child, depth + 1);
                }
            }
        };

        printTree(root, 0);
        std::cout << "\n";

    }
    catch (ErrorAL const& e)
    {
        std::cerr << "AL Error: " << e.what() << ": " << ALStatus_to_string(e.code()) << std::endl;
        return 1;
    }
    catch (std::exception const& e)
    {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

