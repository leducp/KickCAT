#include "kickcat/Link.h"
#include "kickcat/protocol.h"
#include "kickcat/DebugHelpers.h"
#include "kickcat/SocketNull.h"

#include <iostream>

#ifdef __linux__
    #include "kickcat/OS/Linux/Socket.h"
#elif __PikeOS__
    #include "kickcat/OS/PikeOS/Socket.h"
#else
    #error "Unknown platform"
#endif


using namespace kickcat;

int main(int , char* argv[])
{
    std::shared_ptr<AbstractSocket> socketRedundancy;
    std::string red_interface_name = "null";
    std::string nom_interface_name = argv[1];

    socketRedundancy = std::make_shared<SocketNull>();
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
    link->checkRedundancyNeeded();


    // This register (0x800) has to be R/W on the device
    sendWriteRegister<uint8_t>(*link, 0x00, 0x800, 0x0000);

    uint16_t value_read;
    sendGetRegister(*link, 0x00, 0x800, value_read);
    printf("Value (initial) : %04x\n", value_read);

    uint8_t value_write = 0x000f;
    sendWriteRegister<uint8_t>(*link, 0x00, 0x800, value_write);

    sendGetRegister(*link, 0x00, 0x800, value_read);
    printf("Value (modified): %04x\n", value_read);
}
