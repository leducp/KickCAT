#ifndef KICKCAT_DIAGNOSTICS_H
#define KICKCAT_DIAGNOSTICS_H

#include <unordered_map>

#include "kickcat/Link.h"
#include "kickcat/Slave.h"

namespace kickcat
{
    /// \brief return the topology of discovered network - To be called after bus.getDLStatus()
    /// \return [key, value] pair : [slave adress, parent address] (the only slave that is its own parent is linked to the master)
    std::unordered_map<uint16_t, uint16_t> getTopology(std::vector<Slave>& slaves);

    template<typename T>
    void read_address(Slave& slave, uint16_t address, T& value, std::shared_ptr<Link> link)
    {
        auto process = [address, &value](DatagramHeader const*, uint8_t const* data, uint16_t wkc)
        {
            if (wkc != 1)
            {
                return DatagramState::INVALID_WKC;
            }

            std::memcpy(&value, data, sizeof(T));
            if (sizeof(T) > 4)
            {
                printf("0x%04x -> read 0x%02lx\n", address, value);

            }
            else
            {
                printf("0x%04x -> read 0x%02x\n", address, value);
            }
            return DatagramState::OK;
        };

        link->addDatagram(Command::FPRD, createAddress(slave.address, address), nullptr, sizeof(T), process, [](DatagramState const&){});
    }
}

#endif
