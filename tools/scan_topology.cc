#include <iostream>
#include <string>
#include <algorithm>
#include <argparse/argparse.hpp>

#include "kickcat/Bus.h"
#include "kickcat/Diagnostics.h"
#include "kickcat/Link.h"
#include "kickcat/Prints.h"
#include "kickcat/helpers.h"

using namespace kickcat;

int main(int argc, char* argv[])
{
    argparse::ArgumentParser program("scan_topology");

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

    auto reportRedundancy = []()
    {
        printf("Redundancy has been activated due to loss of a cable \n");
    };

    std::shared_ptr<Link> link = std::make_shared<Link>(socket_nominal, socket_redundancy, reportRedundancy);
    link->setTimeout(2ms);
    link->checkRedundancyNeeded();

    Bus bus(link);

    try
    {
        printf("Initializing Bus...\n");
        bus.init(100ms);
        printf("Init done. Detected slaves: %zu\n", bus.slaves().size());
    }
    catch (ErrorAL const& e)
    {
        std::cerr << e.what() << ": " << ALStatus_to_string(e.code()) << std::endl;
        return 1;
    }
    catch (std::exception const& e)
    {
        std::cerr << e.what() << std::endl;
        return 1;
    }

    printf("Fetching DL Status for all slaves...\n");
    auto error_handler = [](DatagramState const& state){ THROW_ERROR_DATAGRAM("Error fetching DL Status", state); };
    for (auto& slave : bus.slaves())
    {
        bus.sendGetDLStatus(slave, error_handler);
    }
    bus.finalizeDatagrams();
    bus.processAwaitingFrames();

    for (auto& slave : bus.slaves())
    {
        printf("Slave %d status:\n%s", slave.address, toString(slave.dl_status).c_str());
    }

    try
    {
        std::unordered_map<uint16_t, uint16_t> topology = getTopology(bus.slaves());
        print(topology);
    }
    catch (std::exception const& e)
    {
        std::cerr << "Error determining topology: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
