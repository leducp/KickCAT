/// Tools to evaluate network quality by displaying number of packet lost or corrupted.
/// Only available on linux for the moment.

#include "kickcat/Link.h"
#include "kickcat/Bus.h"
#include "kickcat/SocketNull.h"

#ifdef __linux__
    #include "kickcat/OS/Linux/Socket.h"
#else
    #error "Unknown platform"
#endif

#include <iostream>
#include <fstream>


using namespace kickcat;

int main(int argc, char* argv[])
{
    if (argc != 3 and argc != 2)
    {
        printf("usage redundancy mode : ./test NIC_nominal NIC_redundancy\n");
        printf("usage no redundancy mode : ./test NIC_nominal\n");
        return 1;
    }


    std::shared_ptr<AbstractSocket> socket_redundancy;
    std::string red_interface_name = "null";
    std::string nom_interface_name = argv[1];

    if (argc == 2)
    {
        printf("No redundancy mode selected \n");
        socket_redundancy = std::make_shared<SocketNull>();
    }
    else
    {
        socket_redundancy = std::make_shared<Socket>();
        red_interface_name = argv[2];
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
    link->setTimeout(500ms);

    Bus bus(link);

    constexpr int64_t LOOP_NUMBER = 12 * 3600 * 1000; // 12h

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
            uint32_t tx_packets, rx_packets, rx_errors;
            file_tx_packets >> tx_packets;
            file_rx_packets >> rx_packets;
            file_rx_errors  >> rx_errors;
            printf("tx_packets: %u, rx_packets: %u, Diff: %i, rx_errors: %u \n", tx_packets, rx_packets, tx_packets - rx_packets, rx_errors);
        }
    }

    return 0;
}
