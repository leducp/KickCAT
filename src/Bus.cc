#include "Bus.h"
#include "AbstractSocket.h"
#include <unistd.h>

#include <cstring>

namespace kickcat
{
    Bus::Bus(std::shared_ptr<AbstractSocket> socket)
        : socket_{socket}
        , frame_{PRIMARY_IF_MAC}
    {

    }


    int32_t Bus::getSlavesOnNetwork()
    {
        uint8_t param = 0;
        frame_.addDatagram(1, Command::BWR, createAddress(0, 0x0101), &param, sizeof(param));

        Error err = frame_.write(socket_);
        if (err) { err.what(); }

        usleep(1000);

        err = frame_.read(socket_);
        if (err) { err.what(); }

        auto [header, data, wkc] = frame_.nextDatagram();

        printf("----> There is %d slaves on the bus\n", wkc);

        return 0;
    }


    Error Bus::init()
    {
        return EERROR("not implemented");
    }


    Error Bus::resetSlaves()
    {

    }
}
