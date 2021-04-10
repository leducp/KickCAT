#include "Protocol.h"
#include "LinuxSocket.h"

using namespace kickcat;

int main()
{
    LinuxSocket socket;
    Error err = socket.open("lo");
    if (err)
    {
        err.what();
        return 1;
    }
    return 0;
}
