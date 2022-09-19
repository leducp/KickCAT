#include "kickcat/Bus.h"
#include "kickcat/LinkRedundancy.h"

#ifdef __linux__
    #include "kickcat/OS/Linux/Socket.h"
#elif __PikeOS__
    #include "kickcat/OS/PikeOS/Socket.h"
#else
    #error "Unknown platform"
#endif

#include <iostream>
#include <fstream>
#include <algorithm>

#include "kickcat/Teleplot.h"
#include "kickcat/DebugHelper.h"

using namespace kickcat;

int main(int argc, char* argv[])
{
    if (argc != 3)
    {
        printf("usage: ./test NIC_nominal NIC_redundancy\n");
        return 1;
    }


    auto reportRedundancy = []()
    {
        printf("Redundancy has been activated due to loss of a cable \n");
    };

    auto socketNominal = std::make_shared<Socket>();
    auto socketRedundancy = std::make_shared<Socket>();

    try
    {
        socketNominal->open(argv[1], 2ms);
        socketRedundancy->open(argv[2], 2ms);
    }
    catch (std::exception const& e)
    {
        std::cerr << e.what() << std::endl;
        return 1;
    }

    Frame frameNominal{PRIMARY_IF_MAC};
    Frame frameRedundancy{SECONDARY_IF_MAC};


    constexpr int64_t LOOP_NUMBER = 12 * 3600 * 1000; // 12h
    for (int64_t i = 0; i < LOOP_NUMBER; ++i)
    {
        sleep(1000ms);


        try
        {
            frameNominal.addDatagram(0, Command::BRD, createAddress(0, reg::TYPE), nullptr, 1);
            frameRedundancy.addDatagram(0, Command::BRD, createAddress(0, reg::TYPE), nullptr, 1);

            frameNominal.write(socketNominal);
            frameRedundancy.write(socketRedundancy);

            frameNominal.read(socketNominal);
            frameRedundancy.read(socketRedundancy);

            frameNominal.clear();
            frameRedundancy.clear();
        }
        catch (std::exception const& e)
        {
            std::cerr << e.what() << " at " << std::endl;
        }

        printf("\n ---------------------------------------------------------------------------- \n");
    }

    return 0;
}
