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

    uint8_t io_buffer[1024];
    err = bus.createMapping(io_buffer);
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
        printf("Slave %04x state is %s\n", slave.address, toString(state));
    }

    bus.requestState(State::OPERATIONAL);
    bus.waitForState(State::OPERATIONAL, 10ms);
    for (auto& slave : bus.slaves())
    {
        State state = bus.getCurrentState(slave);
        printf("Slave %04x state is %s\n", slave.address, toString(state));
    }

    auto& slave = bus.slaves().at(0);
    for (int32_t i = 0; i < 5000; ++i)
    {
        sleep(10ms);
        bus.processDataRead();

        for (int32_t j = 0;  j < slave.input.bsize; ++j)
        {
            printf("%02x ", slave.input.data[j]);
        }
        printf("\r");

        // blink a led - EasyCAT example for Arduino
        if ((i % 50) < 25)
        {
            slave.output.data[0] = 1;
        }
        else
        {
            slave.output.data[0] = 0;
        }
        bus.processDataWrite();
    }
    printf("\n");

    return 0;
}
