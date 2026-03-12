#include <iostream>

#include "helpers.h"

#include "kickcat/TapSocket.h"
#include "kickcat/SocketNull.h"
#ifdef __linux__
#include "kickcat/OS/Linux/Socket.h"
#ifdef KICKCAT_AF_XDP_ENABLED
#include "kickcat/OS/Linux/XdpSocket.h"
#endif
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
            printf("%zd. %s\n", i, netIf.format().c_str());
        }

        for (auto& ifname : interfaces)
        {
            // enable cin exceptions
            std::ios::iostate old_exceptions = std::cin.exceptions();
            std::cin.exceptions(std::ios::failbit | std::ios::badbit | std::ios::eofbit);

            uint32_t index = net_interfaces.size();
            while (index >= net_interfaces.size())
            {
                printf("Which interface should be used for the %s interface?\n", ifname.first);
                std::cin >> index;
            }
            *ifname.second = net_interfaces[index].name;

            // restore cin exception state
            std::cin.exceptions(old_exceptions);
        }
    }


    std::tuple<std::shared_ptr<AbstractSocket>, std::shared_ptr<AbstractSocket>> createSockets(std::string nominal_if, std::string redundant_if)
    {
        std::shared_ptr<AbstractSocket> nominal;
        std::shared_ptr<AbstractSocket> redundant;

        selectInterface(nominal_if, redundant_if);

        auto createSocket = [&](std::string& ifname, std::string const& tap_name) -> std::shared_ptr<AbstractSocket>
        {
            if (ifname == "tap:server")
            {
                ifname = tap_name;
                return std::make_shared<TapSocket>(true);
            }
            if (ifname == "tap:client")
            {
                ifname = tap_name;
                return std::make_shared<TapSocket>(false);
            }
            if (ifname.empty())
            {
                ifname = "None";
                return std::make_shared<SocketNull>();
            }
#ifdef KICKCAT_AF_XDP_ENABLED
            constexpr char XDP_PREFIX[] = "xdp:";
            constexpr size_t XDP_PREFIX_LEN = sizeof(XDP_PREFIX) - 1;
            if (ifname.compare(0, XDP_PREFIX_LEN, XDP_PREFIX) == 0)
            {
                ifname = ifname.substr(XDP_PREFIX_LEN);
                return std::make_shared<XdpSocket>();
            }
#endif
            return std::make_shared<Socket>();
        };

        nominal = createSocket(nominal_if, "tap_nominal");
        printf("Opening nominal interface:   %s\n", nominal_if.c_str());
        nominal->open(nominal_if);

        redundant = createSocket(redundant_if, "tap_redundant");
        printf("Opening redundant interface: %s\n", redundant_if.c_str());
        redundant->open(redundant_if);

        return {nominal, redundant};
    }
// LCOV_EXCL_STOP
}
