#include "kickcat/Bus.h"
#include "kickcat/Diagnostics.h"
#include "kickcat/Prints.h"
#include "kickcat/DebugHelpers.h"

#ifdef __linux__
    #include "kickcat/OS/Linux/Socket.h"
#elif __PikeOS__
    #include "kickcat/OS/PikeOS/Socket.h"
#else
    #error "Unknown platform"
#endif

#include <iostream>
#include <string>
#include <algorithm>

using namespace kickcat;

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
    uint16_t first_error = 0;
    uint16_t error_port = 0xFF;
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

void printBinary(auto data)
{
    
    for (int i = 0; i < 8*sizeof(data); ++i)
    {
        if (data%2 == 1) {printf("1");} else {printf("0");};
        data = data >> 1;
    }
    printf("\n");
}

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        printf("usage: ./test NIC\n");
        return 1;
    }

    auto socket = std::make_shared<Socket>();
    Bus bus(socket);

    auto print_current_state = [&]()
    {
        for (auto& slave : bus.slaves())
        {
            State state = bus.getCurrentState(slave);
            printf("Slave %d state is %s\n", slave.address, toString(state));
        }
    };

    uint8_t io_buffer[2048];
    try
    {
        socket->open(argv[1], 2ms);
        printf("ok");
        bus.init();

        print_current_state();
    }
    catch (ErrorCode const& e)
    {
        std::cerr << e.what() << ": " << ALStatus_to_string(e.code()) << std::endl;
        return 1;
    }
    catch (std::exception const& e)
    {
        std::cerr << e.what() << std::endl;
        return 1;
    }
    for (uint16_t add = 0x100; add <= 0x103; ++add)
    {
        printf("reg: %04x\n", add);
        //sendWriteRegister(bus.link(), 0x0000, add, 0x55);
        uint8_t value_read;
        sendGetRegister(bus.link(), 0x0000, add, value_read);
        printf("Value : %04x - b", value_read);
        printBinary(value_read);
    }

    for (auto& slave : bus.slaves())
    {
        bus.sendGetDLStatus(slave);
        bus.finalizeDatagrams();

        printDLStatus(slave);
    }

    PortsAnalysis(bus.slaves());

    std::unordered_map<uint16_t, uint16_t> topology = getTopology(bus.slaves());

    FILE* top_file = fopen("topology.csv", "w");
    std::string sample_str;
    for (const auto& [key, value] : topology)
    {
        sample_str = std::to_string(key);
        fwrite(sample_str.data(), 1, sample_str.size(), top_file);
        fwrite(",", 1, 1, top_file);
        sample_str = std::to_string(value);
        fwrite(sample_str.data(), 1, sample_str.size(), top_file);
        fwrite("\n", 1, 1, top_file);

    }
    PortsAnalysis(bus.slaves());
    printTopology(topology);
    fclose(top_file);
    printf("File written");
}
