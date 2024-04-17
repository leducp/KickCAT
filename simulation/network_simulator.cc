#include "kickcat/OS/Linux/Socket.h"
#include "kickcat/Frame.h"
#include "kickcat/ESC/EmulatedESC.h"

#include "kickcat/CoE/EsiParser.h"
#include "kickcat/CoE/mailbox/response.h"

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

    std::vector<EmulatedESC> escs;
    for (int i = 2 ; i < argc ; ++i)
    {
        escs.emplace_back(argv[i]);
    }

    CoE::EsiParser parser;
    auto& coe_dict = CoE::dictionary();
    coe_dict = parser.load("ingenia_esi.xml");

    printf("Start EtherCAT network simulator on %s with %ld slaves\n", argv[1], escs.size());
    auto socket = std::make_shared<Socket>(-1ns, 1us);
    socket->open(argv[1]);
    socket->setTimeout(-1ns);

    std::vector<nanoseconds> stats;
    stats.reserve(1000);

    auto const mbx_out_cfg  = SYNC_MANAGER_MBX_OUT (0, 0x1000, 128);
    auto const mbx_in_cfg = SYNC_MANAGER_MBX_IN(1, 0x1400, 128);
    auto& esc0 = escs.at(0);
    mailbox::response::Mailbox mbx(&esc0, mbx_in_cfg, mbx_out_cfg);
    mbx.enableCoE();

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

                mbx.receive();
                mbx.process();
                mbx.send();
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
