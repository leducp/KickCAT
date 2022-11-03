#include "kickcat/Bus.h"
#include "kickcat/Diagnostics.h"
#include "kickcat/Prints.h"

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

    socket->setTimeout(1ms);

    for (auto& slave : bus.slaves())
    {
        bus.sendGetDLStatus(slave, [](DatagramState const& state){THROW_ERROR_DATAGRAM("Error fetching DL Status", state);});
        bus.finalizeDatagrams();

        printDLStatus(slave);
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
