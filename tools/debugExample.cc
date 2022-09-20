#include "kickcat/Link.h"
#include "kickcat/protocol.h"
#include "kickcat/DebugHelpers.h"

#include <iostream>

#ifdef __linux__
    #include "kickcat/OS/Linux/Socket.h"
#elif __PikeOS__
    #include "kickcat/OS/PikeOS/Socket.h"
#else
    #error "Unknown platform"
#endif


using namespace kickcat;

int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        printf("usage: ./test NIC\n");
        return 1;
    }

    auto socket = std::make_shared<Socket>();
    Link link(socket);

    try
    {
        socket->open(argv[1], 2ms);
        socket->setTimeout(1ms);

    }
    catch (std::exception const& e)
    {
        std::cerr << e.what() << std::endl;
        return 1;
    }


    // This register (0x800) has to be R/W on the device
    sendWriteRegister<uint8_t>(link, 0x00, 0x800, 0x0000);

    uint16_t value_read;
    sendGetRegister(link, 0x00, 0x800, value_read);
    printf("Value (initial) : %04x\n", value_read);

    uint8_t value_write = 0x000f;
    sendWriteRegister<uint8_t>(link, 0x00, 0x800, value_write);

    sendGetRegister(link, 0x00, 0x800, value_read);
    printf("Value (modified): %04x\n", value_read);
}
