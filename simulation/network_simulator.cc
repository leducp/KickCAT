#include "kickcat/OS/Linux/Socket.h"
#include "kickcat/Frame.h"
#include "ESC.h"

using namespace kickcat;


int main(int argc, char* argv[])
{
    if (argc != 3)
    {
        printf("Usage: ./network_simulator interface_name slave_number");
        return -1;
    }


    int32_t const slave_number = strtol(argv[2], nullptr, 10);
    if ((slave_number < 0) or (slave_number > UINT16_MAX))
    {
        printf("Slave number shall be comprised between 0 and %d, got %d\nAborting...\n", UINT16_MAX, slave_number);
        return -1;
    }

    printf("Start EtherCAT network simulator on %s with %d slaves\n", argv[1], slave_number);
    auto socket = std::make_shared<Socket>(-1ns, 1us);
    socket->open(argv[1]);
    socket->setTimeout(-1ns);

    std::vector<ESC> slaves;
    slaves.reserve(slave_number);
    for (int i = 0; i < slave_number; ++i)
    {
        slaves.emplace_back("EasyCAT_32_32_rev_1.bin");
    }

    int32_t x = 0;
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

            //printf("Process new datagram - %s @ %x \n", toString(header->command), header->address);
            for (auto& slave : slaves)
            {
                slave.processDatagram(header, data, wkc);
            }


            slaves.at(0).write(0x1000, &x, 4);
            x += 32;

            if (x < 32768)
            {
                uint16_t yay = hton<uint16_t>(0xCAFE);
                slaves.at(0).write(0x1010, &yay, 2);
            }
            else if (x < 65535)
            {
                uint16_t yay = hton<uint16_t>(0xDECA);
                slaves.at(0).write(0x1010, &yay, 2);
            }
            else
            {
                x = 0;
            }
        }

        //printf("write back %d\n\n", r);
        int32_t written = socket->write(frame.data(), r);
        if (written < 0)
        {
            printf("Write back frame: something wrong happened. Aborting...\n");
            return -2;
        }
        auto t2 = since_epoch();
        //printf("[%f] frame processing time: %ld\n", seconds_f(since_start()).count(), (t2 - t1).count());
    }

    return 0;
}
