#ifndef KICKCAT_NETWORK_INTERFACE
#define KICKCAT_NETWORK_INTERFACE

#include <memory>

#include "AbstractSocket.h"
#include "Frame.h"


namespace kickcat
{

class NetworkInterface
{
public:
    NetworkInterface(std::shared_ptr<AbstractSocket> socket, uint8_t const src_mac[MAC_SIZE]);

    void write(Frame& frame);
    void read(Frame& frame);

private:
    std::shared_ptr<AbstractSocket> socket_;
    uint8_t src_mac_[MAC_SIZE];
};

}
#endif
