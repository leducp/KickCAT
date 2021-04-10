#include "Bus.h"
#include "LinuxSocket.h"

using namespace kickcat;

int main()
{
    auto socket = std::make_unique<LinuxSocket>();
    Error err = socket->open("enp0s31f6");
    if (err)
    {
        err.what();
        return 1;
    }

    Bus bus(std::move(socket));
    bus.getSlavesOnNetwork();
    return 0;
}
