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
        void addDatagram(uint8_t index, enum Command command, uint16_t ADP, uint16_t ADO, void* data, uint16_t data_size);

        std::unique_ptr<AbstractSocket> socket_;
        std::array<uint8_t, ETH_MAX_SIZE> frame_;

        EthercatHeader* header_;
        uint8_t* ethercat_payload_;
        uint8_t* current_pos_;
    };
}

#endif
