#include "kickcat/AbstractSocket.h"

namespace kickcat
{
    std::string NetworkInterface::format() const
    {
        std::string output = name;
        if (not description.empty())
        {
            output += " (" + description + ")";
        }
        return name;
    }

    std::vector<NetworkInterface> listInterfaces()
    {
        return {}; // Currently no socket supported so no interface to discover/list
    }
}
