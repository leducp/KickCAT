#include "kickcat/OS/Linux/VirtualSocket.h"
#include "kickcat/Frame.h"

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
    VirtualSocket::createInterface(argv[1]);
    auto socket = std::make_shared<VirtualSocket>(100us);
    socket->open(argv[1]);
    socket->setTimeout(-1ns);

    while (true)
    {
        Frame frame;
        int32_t r = socket->read(frame.data(), ETH_MAX_SIZE);
        if (r < 0)
        {
            printf("Something wrong happened. Aborting...\n");
            return -1;
        }
        printf("read frame %d - %d\n", r, frame.header()->len);

        while (true)
        {
            auto [header, data, wkc] = frame.peekDatagram();
            if (header == nullptr)
            {
                break;
            }
            printf("ouiiiiii %s @ %x \n", toString(header->command), header->address);

            if ((header->command == Command::BRD) or (header->command == Command::BWR))
            {
                *wkc += slave_number;
            }
        }

        printf("write back %d\n\n", r);
        int32_t written = socket->write(frame.data(), r);
    }

    return 0;
}
