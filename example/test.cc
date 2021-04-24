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
    if (err)
    {
        err.what();
        return 1;
    }

    for (auto& slave : bus.slaves())
    {
        State state = bus.getCurrentState(slave);
        printf("Slave %d state is %s - %x\n", slave.address, toString(state), state);
    }

    bus.printSlavesInfo();

    err = bus.createMapping(nullptr); // this one should explode
    if (err)
    {
        err.what();
        return 1;
    }

    bus.requestState(State::SAFE_OP);
    bus.waitForState(State::SAFE_OP, 10ms);

    for (auto& slave : bus.slaves())
    {
        State state = bus.getCurrentState(slave);
        printf("Slave %d state is %s - %x\n", slave.address, toString(state), state);
    }

    return 0;
}
