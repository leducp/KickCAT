#include "kickcat/OS/Linux/Socket.h"
#include "kickcat/Frame.h"
#include "ESC.h"

#include <cstring>
#include <numeric>

using namespace kickcat;


int main(int argc, char* argv[])
{
    if (argc < 3)
    {
        printf("Usage: ./network_simulator interface_name eeprom_s1.bin ... eeprom_sn.bin");
        return -1;
    }

    std::vector<ESC> slaves;
    for (int i = 2 ; i < argc ; ++i)
    {
        slaves.emplace_back(argv[i]);
    }

    printf("Start EtherCAT network simulator on %s with %ld slaves\n", argv[1], slaves.size());
    auto socket = std::make_shared<Socket>(-1ns, 1us);
    socket->open(argv[1]);
    socket->setTimeout(-1ns);

    std::vector<nanoseconds> stats;
    stats.reserve(1000);

    while (true)
    {
        Frame frame;
        int32_t r = socket->read(frame.data(), ETH_MAX_SIZE);
        if (r < 0)
        {
            printf("Something wrong happened. Aborting...\n");
            return -1;
        }

        auto t1 = since_epoch();
        while (true)
        {
            auto [header, data, wkc] = frame.peekDatagram();
            if (header == nullptr)
            {
                break;
            }

            for (auto& slave : slaves)
            {
                slave.processDatagram(header, data, wkc);
            }
        }

        int32_t written = socket->write(frame.data(), r);
        if (written < 0)
        {
            printf("Write back frame: something wrong happened. Aborting...\n");
            return -2;
        }
        auto t2 = since_epoch();

        stats.push_back(t2 - t1);
        if (stats.size() >= 1000)
        {
            std::sort(stats.begin(), stats.end());

            printf("[%f] frame processing time: \n\t min: %f\n\t max: %f\n\t avg: %f\n", seconds_f(since_start()).count(),
                stats.front().count() / 1000.0,
                stats.back().count()  / 1000.0,
                (std::reduce(stats.begin(), stats.end()) / stats.size()).count() / 1000.0);
            stats.clear();
        }

    }

    return 0;
}
