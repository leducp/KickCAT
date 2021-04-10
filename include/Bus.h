#ifndef KICKCAT_BUS_H
#define KICKCAT_BUS_H

#include <memory>

#include "Error.h"
#include "protocol.h"

namespace kickcat
{
    class AbstractSocket;

    class Bus
    {
    public:
        Bus(std::unique_ptr<AbstractSocket> socket);
        ~Bus() = default;

        int32_t getSlavesOnNetwork();

    private:
        std::unique_ptr<AbstractSocket> socket_;
        std::array<uint8_t, ETH_MAX_SIZE> frame_;

        EthercatHeader* header_;
        uint8_t* ethercat_payload_;
    };
}

#endif
