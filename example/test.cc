#include "Bus.h"
#include "LinuxSocket.h"

#include <iostream>

using namespace kickcat;

namespace elmo
{
    constexpr uint32_t IDENTITY = 0x1018;
    constexpr uint32_t USER_INTEGER = 0x2F00;
    constexpr uint32_t USER_FLOAT = 0x2F01;
}

int main()
{
    auto socket = std::make_shared<LinuxSocket>();
    Bus bus(socket);
    try
    {
        socket->open("enp0s31f6", 1us);
        bus.init();

        for (auto& slave : bus.slaves())
        {
            State state = bus.getCurrentState(slave);
            printf("Slave %d state is %s - %x\n", slave.address, toString(state), state);
        }

        bus.printSlavesInfo();

        uint8_t io_buffer[1024];
        bus.createMapping(io_buffer);

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
    }
    catch (std::exception const& e)
    {
        std::cerr << e.what() << std::endl;
        return 1;
    }
    catch (char const* e)
    {
        std::cerr << e << std::endl;
        return 2;
    }

    auto& slave = bus.slaves().at(0);
    for (int32_t i = 0; i < 5000; ++i)
    {
        sleep(10ms);

        try
        {
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
        catch (std::exception const& e)
        {
            std::cerr << e.what() << std::endl;
            try
            {
                bus.refreshErrorCounters();
                for (auto const& slave : bus.slaves())
                {
                    slave.printErrorCounters();
                    State state = bus.getCurrentState(slave);
                    printf("Slave %04x state is %s\n", slave.address, toString(state));
                }
                bus.requestState(State::OPERATIONAL); // auto recover for deco/reco
            }
            catch (...)
            {
                // do nothing here: we already trying to get back to work
            }
        }
    }
    printf("\n");
    return 0;
}
