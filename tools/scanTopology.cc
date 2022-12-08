#include "kickcat/Bus.h"
#include "kickcat/Diagnostics.h"
#include "kickcat/Prints.h"
#include "kickcat/SocketNull.h"

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

int main(int, char* argv[])
{
    std::shared_ptr<AbstractSocket> socketRedundancy;
    std::string red_interface_name = "null";
    std::string nom_interface_name = argv[1];

    socketRedundancy = std::make_shared<SocketNull>();
    auto socketNominal = std::make_shared<Socket>();
    try
    {
        socketNominal->open(nom_interface_name);
        socketRedundancy->open(red_interface_name);
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

    std::shared_ptr<Link> link= std::make_shared<Link>(socketNominal, socketRedundancy, reportRedundancy);
    link->checkRedundancyNeeded();

    Bus bus(link);

    auto print_current_state = [&]()
    {
        for (auto& slave : bus.slaves())
        {
            State state = bus.getCurrentState(slave);
            printf("Slave %d state is %s\n", slave.address, toString(state));
        }
    };

    try
    {
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

    for (auto& slave : bus.slaves())
    {
        bus.sendGetDLStatus(slave, [](DatagramState const& state){THROW_ERROR_DATAGRAM("Error fetching DL Status", state);});
        bus.finalizeDatagrams();
        printf("%s", toString(slave.dl_status).c_str());
    }

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

    print(topology);

    fclose(top_file);
    printf("File written");
}
