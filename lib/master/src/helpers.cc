#include <iostream>

#include "helpers.h"

#include "kickcat/TapSocket.h"
#include "kickcat/SocketNull.h"
#ifdef __linux__
#include "kickcat/OS/Linux/Socket.h"
#elif __PikeOS__
    #include "kickcat/OS/PikeOS/Socket.h"
#elif __MINGW64__
#include "kickcat/OS/Windows/Socket.h"
#elif __NuttX__
#include "kickcat/OS/NuttX/Socket.h"
#else
#error "Unsupported platform"
#endif

namespace kickcat
{
// LCOV_EXCL_START
    void selectInterface(std::string& nominal_if, std::string& redundant_if)
    {
        std::vector<std::pair<char const*, std::string*>> interfaces;
        if (nominal_if == "?")
        {
            interfaces.push_back({"nominal", &nominal_if});
        }
        if (redundant_if == "?")
        {
            interfaces.push_back({"redundant", &redundant_if});
        }

        if (interfaces.empty())
        {
            return; // No interactive choice asked: nothing to do
        }

        auto net_interfaces = listInterfaces();

        // Add KickCAT tap variation
        net_interfaces.push_back({"tap:server", "KickCAT TAP socket - server"});
        net_interfaces.push_back({"tap:client", "KickCAT TAP socket - client"});
        for (std::size_t i = 0; i < net_interfaces.size(); ++i)
        {
            auto const& netIf = net_interfaces[i];
            printf("%lu. %s\n", i, netIf.format().c_str());
        }

        for (auto& ifname : interfaces)
        {
            uint32_t index = net_interfaces.size();
            while (index >= net_interfaces.size())
            {
                printf("Which interface should be used for the %s interface?\n", ifname.first);
                std::cin >> index;
            }
            *ifname.second = net_interfaces[index].name;
        }
    }


    std::tuple<std::shared_ptr<AbstractSocket>, std::shared_ptr<AbstractSocket>> createSockets(std::string nominal_if, std::string redundant_if)
    {
        std::shared_ptr<AbstractSocket> nominal;
        std::shared_ptr<AbstractSocket> redundant;

        selectInterface(nominal_if, redundant_if);

        if (nominal_if == "tap:server")
        {
            nominal_if = "tap_nominal";
            nominal = std::make_shared<TapSocket>(true);
        }
        else if (nominal_if == "tap:client")
        {
            nominal_if = "tap_nominal";
            nominal = std::make_shared<TapSocket>(false);
        }
        else if (nominal_if.empty())
        {
            nominal = std::make_shared<SocketNull>();
        }
        else
        {
            nominal = std::make_shared<Socket>();
        }
        printf("Opening %s\n", nominal_if.c_str());
        nominal->open(nominal_if);

        if (redundant_if == "tap:server")
        {
            redundant_if = "tap_redundant";
            redundant = std::make_shared<TapSocket>(true);
        }
        else if (redundant_if == "tap:client")
        {
            redundant_if = "tap_redundant";
            redundant = std::make_shared<TapSocket>(false);
        }
        else if (redundant_if.empty())
        {
            redundant = std::make_shared<SocketNull>();
        }
        else
        {
            redundant = std::make_shared<Socket>();
        }
        printf("Opening %s\n", redundant_if.c_str());
        redundant->open(redundant_if);

        return {nominal, redundant};
    }
// LCOV_EXCL_STOP
}
