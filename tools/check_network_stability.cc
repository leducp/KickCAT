/// Tools to evaluate network quality by displaying number of packet lost or corrupted.
/// Only available on linux for the moment.

#include <iostream>
#include <fstream>
#include <argparse/argparse.hpp>

#include "kickcat/Link.h"
#include "kickcat/Bus.h"
#include "kickcat/helpers.h"
#include "kickcat/Prints.h"
#include "kickcat/Error.h"

using namespace kickcat;

int main(int argc, char* argv[])
{
    argparse::ArgumentParser program("check_network_stability");

    std::string nom_interface_name;
    program.add_argument("-i", "--interface")
        .help("network interface name")
        .required()
        .store_into(nom_interface_name);

    std::string red_interface_name;
    program.add_argument("-r", "--redundancy")
        .help("redundancy network interface name")
        .default_value(std::string{""})
        .store_into(red_interface_name);

    try
    {
        program.parse_args(argc, argv);
    }
    catch (const std::runtime_error& err)
    {
        std::cerr << err.what() << std::endl;
        std::cerr << program;
        return 1;
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
        std::cerr << e.what() << std::endl;
        return 1;
    }

    auto report_redundancy = []()
    {
        printf("Redundancy has been activated due to loss of a cable \n");
    };

    std::shared_ptr<Link> link = std::make_shared<Link>(socket_nominal, socket_redundancy, report_redundancy);
    link->setTimeout(500ms);
    link->checkRedundancyNeeded();

    Bus bus(link);

    constexpr int64_t LOOP_NUMBER = 6 * 3600 * 1000; // 6h

    int64_t last_error = 0;
    nanoseconds last_time = 0s;

    auto is_file_open = [](std::ifstream const& file, std::string const& name)
    {
        if (not file.is_open())
        {
            printf("Could not open file: %s \n", name.c_str());
            return false;
        }
        return true;
    };

    std::string tx_packets_path = "/sys/class/net/" + nom_interface_name + "/statistics/tx_packets";
    std::string rx_packets_path = "/sys/class/net/" + nom_interface_name + "/statistics/rx_packets";
    std::string rx_errors_path = "/sys/class/net/" + nom_interface_name + "/statistics/rx_errors";

    std::ifstream file_tx_packets(tx_packets_path);
    std::ifstream file_rx_packets(rx_packets_path);
    std::ifstream file_rx_errors(rx_errors_path);

    if (not (    is_file_open(file_tx_packets, tx_packets_path)
             and is_file_open(file_rx_packets, rx_packets_path)
             and is_file_open(file_rx_errors, rx_errors_path)))
    {
        return -1;
    }

    printf("Starting network stability check on %s (redundancy: %s)...\n", 
           nom_interface_name.c_str(), red_interface_name.empty() ? "none" : red_interface_name.c_str());

    for (int64_t i = 0; i < LOOP_NUMBER; ++i)
    {
        sleep(1ms);

        try
        {
            bus.broadcastRead(reg::TYPE, 1);
        }
        catch (std::exception const& e)
        {
            int64_t delta = i - last_error;
            last_error = i;
            std::cerr << e.what() << " at " << i << " delta: " << delta << std::endl;
        }

        if (elapsed_time(last_time) > 5s)
        {
            last_time = since_epoch();
            uint32_t tx_packets = 0, rx_packets = 0, rx_errors = 0;
            
            file_tx_packets.clear();
            file_tx_packets.seekg(0);
            file_tx_packets >> tx_packets;
            
            file_rx_packets.clear();
            file_rx_packets.seekg(0);
            file_rx_packets >> rx_packets;
            
            file_rx_errors.clear();
            file_rx_errors.seekg(0);
            file_rx_errors >> rx_errors;

            printf("tx_packets: %u, rx_packets: %u, Diff: %d, rx_errors: %u \n", 
                   tx_packets, rx_packets, static_cast<int>(tx_packets - rx_packets), rx_errors);
        }
    }

    return 0;
}
