#include <iostream>

#include "helpers.h"
#include "AbstractSocket.h"

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

        auto netIntefaces = listInterfaces();
        for (std::size_t i = 0; i < netIntefaces.size(); ++i)
        {
            auto const& netIf = netIntefaces[i];
            printf("%lu. %s\n", i, netIf.format().c_str());
        }

        for (auto& ifname : interfaces)
        {
            uint32_t index = netIntefaces.size();
            while (index >= netIntefaces.size())
            {
                printf("Which interface should be used for the %s interface?\n", ifname.first);
                std::cin >> index;
            }
            *ifname.second = netIntefaces[index].name;
        }
    }
// LCOV_EXCL_STOP
}
