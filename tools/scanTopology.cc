#include "kickcat/Bus.h"
#include "kickcat/Diagnostics.h"
#include "kickcat/protocol.h"
#include "kickcat/Prints.h"

#ifdef __linux__
    #include "kickcat/OS/Linux/Socket.h"
#elif __PikeOS__
    #include "kickcat/OS/PikeOS/Socket.h"
#else
    #error "Unknown platform"
#endif

#include <iostream>
#include <fstream>
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

        Slave& pelvis = bus.slaves().at(0);
        pelvis.is_static_mapping = true;
        pelvis.input.bsize = 356;
        pelvis.output.bsize = 8;
        pelvis.input.sync_manager = 2;
        pelvis.output.sync_manager = 1;

        Slave& ankle = bus.slaves().at(1);
        ankle.is_static_mapping = true;
        ankle.input.bsize = 114;
        ankle.output.bsize = 4;
        ankle.input.sync_manager = 2;
        ankle.output.sync_manager = 1;

        print_current_state();

        bus.createMapping(io_buffer);

        bus.requestState(State::SAFE_OP);
        bus.waitForState(State::SAFE_OP, 1s);
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

    auto callback_error = [](DatagramState const&){ THROW_ERROR("something bad happened"); };

    try
    {
        bus.processDataRead(callback_error);
    }
    catch (...)
    {
        //We do not know expected wkc - To be completed if we do
    }

    try
    {
        //bus.requestState(State::OPERATIONAL);
        //bus.waitForState(State::OPERATIONAL, 100ms);
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
        bus.sendGetDLStatus(slave);
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

    printTopology(topology);

    fclose(top_file);
    printf("File written");
}
