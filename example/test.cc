#include "Bus.h"
#include "LinuxSocket.h"

#include <unistd.h>

using namespace kickcat;

int main()
{
    auto socket = std::make_shared<LinuxSocket>();
    Error err = socket->open("enp0s31f6");
    if (err)
    {
        err.what();
        return 1;
    }

    Bus bus(socket);

    err = bus.init();
    err.what();
    return 0;
}
