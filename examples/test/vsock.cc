#include "kickcat/OS/Linux/VirtualSocket.h"

using namespace kickcat;

void produce(AbstractSocket& socket)
{
    int i = 0;
    while (true)
    {
        int32_t written = socket.write(reinterpret_cast<uint8_t const*>(&(++i)), sizeof(int));
        printf("Written %d bytes - value: %d\n", written, i);
        sleep(100us);
    }
}


void consumme(AbstractSocket& socket)
{
    socket.setTimeout(2s);
    while (true)
    {
        int i;
        int32_t rec = socket.read(reinterpret_cast<uint8_t*>(&i), sizeof(int));
        printf("Read %d bytes - value: %d\n", rec, i);
    }
}


int main(int argc, char* argv[])
{
    if (argc != 2)
    {
        printf("Usage: ./binary interface_name");
        return -1;
    }

#ifdef SIDE_A
    printf("Side A - init %s\n", argv[1]);
    VirtualSocket::createInterface(argv[1]);
    VirtualSocket socket(100us, true);
    socket.open(argv[1]);
    produce(socket);

#else
    printf("Side B\n");
    VirtualSocket socket(100us, false);
    socket.open(argv[1]);
    consumme(socket);
#endif

}
