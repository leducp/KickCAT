// Tool to dump and flash the eeprom of a slave on the bus.
// Current state: POC, can only dump eeprom file.

#include "kickcat/Bus.h"
#include "kickcat/Prints.h"
#include "kickcat/SocketNull.h"

#ifdef __linux__
    #include "kickcat/OS/Linux/Socket.h"
#elif __PikeOS__
    #include "kickcat/OS/PikeOS/Socket.h"
#else
    #error "Unknown platform"
#endif

#include <fstream>
#include <iostream>

using namespace kickcat;

int main(int argc, char* argv[])
{
    if ((argc != 5) and (argc != 6))
    {
        printf("argc: %d\n", argc);
        printf("usage redundancy mode :    ./eeprom [slave_number] [command] [file] NIC_nominal NIC_redundancy\n");
        printf("usage no redundancy mode : ./eeprom [slave_number] [command] [file] NIC_nominal\n");
        return 1;
    }


    std::shared_ptr<AbstractSocket> socket_redundancy;
    int slave_index     = std::stoi(argv[1]);
    std::string command = argv[2];
    std::string file    = argv[3];
    std::string red_interface_name = "null";
    std::string nom_interface_name = argv[4];

    if (argc == 5)
    {
        printf("No redundancy mode selected \n");
        socket_redundancy = std::make_shared<SocketNull>();
    }
    else
    {
        socket_redundancy = std::make_shared<Socket>();
        red_interface_name = argv[5];
    }

    auto socket_nominal = std::make_shared<Socket>();
    try
    {
        socket_nominal->open(nom_interface_name);
        socket_redundancy->open(red_interface_name);
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

    std::shared_ptr<Link> link= std::make_shared<Link>(socket_nominal, socket_redundancy, report_redundancy);
    link->setTimeout(10ms);
    link->checkRedundancyNeeded();

    Bus bus(link);

    try
    {
        bus.init();
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

    auto const& slave = bus.slaves().at(slave_index);
    auto const& sii = slave.sii.buffer;
    char const* raw_data = reinterpret_cast<char const*>(sii.data());
    std::ofstream f(file, std::ofstream::binary);

    // Create an eeprom binary file with the right size of empty data
    std::vector<char> empty(slave.eeprom_size, -1);
    f.write(empty.data(), empty.size());
    f.seekp(0);

    // Write dumped data
    f.write(raw_data, sii.size() * 4);

    return 0;
}
