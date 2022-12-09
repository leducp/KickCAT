#include <iostream>
#include <cstring>

#include "kickcat/Link.h"
#include "kickcat/Bus.h"
#include "kickcat/Prints.h"
#include "kickcat/SocketNull.h"
#include "kickcat/Gateway.h"

#ifdef __linux__
    #include "kickcat/OS/Linux/Socket.h"
#elif __PikeOS__
    #include "kickcat/OS/PikeOS/Socket.h"
#else
    #error "Unknown platform"
#endif


#include "kickcat/OS/Linux/UdpDiagSocket.h"


using namespace kickcat;
using namespace std::placeholders;

int main(int argc, char* argv[])
{
    if (argc != 3 and argc != 2)
    {
        printf("usage redundancy mode : ./test NIC_nominal NIC_redundancy\n");
        printf("usage no redundancy mode : ./test NIC_nominal\n");
        return 1;
    }


    std::shared_ptr<AbstractSocket> socketRedundancy;
    std::string red_interface_name = "null";
    std::string nom_interface_name = argv[1];

    if (argc == 2)
    {
        printf("No redundancy mode selected \n");
        socketRedundancy = std::make_shared<SocketNull>();
    }
    else
    {
        socketRedundancy = std::make_shared<Socket>();
        red_interface_name = argv[2];
    }

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
    link->setTimeout(2ms);
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

        printf("Init done \n");
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

    auto socket = std::make_shared<UdpDiagSocket>();
    socket->open();
    Gateway gateway(socket, std::bind(&Bus::addGatewayMessage, &bus, _1, _2, _3));

    auto callback_error = [](DatagramState const&){ THROW_ERROR("something bad happened"); };
    constexpr int64_t LOOP_NUMBER = 12 * 3600 * 1000; // 12h
    for (int64_t i = 0; i < LOOP_NUMBER; ++i)
    {
        sleep(10ms);

        try
        {
            if (i % 2)
            {
                bus.sendMailboxesReadChecks(callback_error);
                bus.sendMailboxesWriteChecks(callback_error);
            }
            else
            {
                bus.sendReadMessages(callback_error);
                bus.sendWriteMessages(callback_error);
            }
            bus.finalizeDatagrams();

            bus.processAwaitingFrames();
        }
        catch (std::exception const& e)
        {
            std::cerr << e.what() << std::endl;
        }

        // Process gateway pending messages
        gateway.processPendingRequests();

        // Get new message request from gateway if any
        gateway.fetchRequest();
    }

    return 0;
}
