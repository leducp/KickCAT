#include "kickcat/OS/Linux/VirtualSocket.h"

using namespace kickcat;

[[noreturn]] void produce(AbstractSocket& socket)
{
    int i = 0;
    while (true)
    {
        int32_t written = socket.write(reinterpret_cast<uint8_t const*>(&(++i)), sizeof(int));
        printf("Written %d bytes - value: %d\n", written, i);
        sleep(100us);
    }
}


[[noreturn]] void consumme(AbstractSocket& socket)
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
    if (argc != 3)
    {
        printf("Usage: ./binary interface_name producer/consummer");
        return -1;
    }

    if (std::string(argv[2]) == "producer")
    {
        printf("Producer - init %s\n", argv[1]);
        VirtualSocket::createInterface(argv[1]);
        VirtualSocket socket(100us);
        socket.open(argv[1]);
        produce(socket);
    }
}
