#include <algorithm>
#include <cstring>
#include <numeric>

#include "kickcat/TapSocket.h"
#ifdef __linux__
#include "kickcat/OS/Linux/Socket.h"
#elif __MINGW64__
#include "kickcat/OS/Windows/Socket.h"
#else
#error "Unsupported platform"
#endif

#include "kickcat/ESC/EmulatedESC.h"
#include "kickcat/Frame.h"
#include "kickcat/slave/Slave.h"

#include "kickcat/CoE/EsiParser.h"
#include "kickcat/CoE/mailbox/response.h"


using namespace kickcat;
using namespace kickcat::slave;


int main(int argc, char* argv[])
{
    if (argc < 3)
    {
        printf("Usage: ./network_simulator interface_name eeprom_s1.bin ... eeprom_sn.bin");
        return -1;
    }

    size_t slaveCount = argc - 2;
    std::vector<EmulatedESC> escs;
    std::vector<PDO> pdos;
    std::vector<Slave> slaves;
    std::vector<uint8_t*> inputPdo;
    std::vector<uint8_t*> outputPdo;

    escs.reserve(slaveCount);
    pdos.reserve(slaveCount);
    slaves.reserve(slaveCount);
    inputPdo.reserve(slaveCount);
    outputPdo.reserve(slaveCount);
    for (int i = 2; i < argc; ++i)
    {
        escs.emplace_back(argv[i]);
        pdos.emplace_back(&escs.back());
        slaves.emplace_back(&escs.back(), &pdos.back());

        inputPdo.push_back(new uint8_t[1024]);
        outputPdo.push_back(new uint8_t[1024]);
        pdos.back().setInput(inputPdo.back());
        pdos.back().setOutput(outputPdo.back());
    }

    CoE::EsiParser parser;
    auto coe_dict = parser.loadFile("foot.xml");

    printf("Start EtherCAT network simulator on %s with %ld slaves\n", argv[1], escs.size());
    auto socket = std::make_shared<TapSocket>(true);
    //auto socket = std::make_shared<Socket>();
    socket->open(argv[1]);
    socket->setTimeout(-1ns);

    std::vector<nanoseconds> stats;
    stats.reserve(1000);

    auto& esc0   = escs.at(0);
    auto& slave0 = slaves.at(0);
    mailbox::response::Mailbox mbx(&esc0, 1024);
    mbx.enableCoE(std::move(coe_dict));
    slave0.setMailbox(&mbx);


    for (auto& slave : slaves)
    {
        slave.start();
    }


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

            for (auto& esc : escs)
            {
                //auto raw = t1.count();
                //esc.write(0x1800, &raw, sizeof(decltype(raw)));
                esc.processDatagram(header, data, wkc);
            }

            for (auto& slave : slaves)
            {
                slave.routine();
                if (slave.state() == State::SAFE_OP)
                {
                    slave.validateOutputData();
                }
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
