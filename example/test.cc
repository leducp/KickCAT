#include "Bus.h"
#include "LinuxSocket.h"

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
    bus.getSlavesOnNetwork();
    return 0;
}
