#include "Bus.h"
#include "LinuxSocket.h"

#include <iostream>
#include <algorithm>

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

        uint8_t io_buffer[1024];
        bus.createMapping(io_buffer);
    }
    catch (std::exception const& e)
    {
        std::cerr << e.what() << std::endl;
        return 1;
    }

    try
    {
        bus.requestState(State::SAFE_OP);
        bus.waitForState(State::SAFE_OP, 10s);
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
    catch (ErrorCode const& e)
    {
        std::cerr << e.what() << ": " << ALStatus_to_string(e.code()) << std::endl;
        return 1;
    }
    catch (std::exception const& e)
    {
        std::cerr << e.what() << std::endl;
        return 1;
    }
/*
    constexpr int32_t LOOP_NUMBER = 10000;
    std::vector<nanoseconds> stats;
    stats.resize(LOOP_NUMBER);

    auto& easycat = bus.slaves().at(0);
    for (int32_t i = 0; i < LOOP_NUMBER; ++i)
    {
        sleep(5ms);

        try
        {
            nanoseconds t1 = since_epoch();

            bus.processDataRead();

            for (int32_t j = 0;  j < easycat.input.bsize; ++j)
            {
                printf("%02x ", easycat.input.data[j]);
            }
            printf("\r");

            // blink a led - EasyCAT example for Arduino
            if ((i % 50) < 25)
            {
                easycat.output.data[0] = 1;
            }
            else
            {
                easycat.output.data[0] = 0;
            }
            bus.processDataWrite();

            // handle messages
            bus.processMessages();

            nanoseconds t4 = since_epoch();
            stats[i] = t4 - t1;
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

    std::sort(stats.begin(), stats.end());
    printf("min %03ldus max %03ldus med %03ldus\n",
        duration_cast<microseconds>(stats.at(1)).count(),
        duration_cast<microseconds>(stats.at(stats.size() - 2)).count(),
        duration_cast<microseconds>(stats.at(stats.size() / 2)).count());
*/
    return 0;
}
