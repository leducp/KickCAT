#include <cstring>

#include "kickcat/AbstractLink.h"
#include "kickcat/Error.h"

namespace kickcat
{
    void readRegister(AbstractLink& link, uint16_t slave_address, uint16_t reg_address, void* value, uint16_t size)
    {
        auto process = [value, size](DatagramHeader const*, uint8_t const* data, uint16_t wkc)
        {
            if (wkc != 1)
            {
                return DatagramState::INVALID_WKC;
            }
            std::memcpy(value, data, size);
            return DatagramState::OK;
        };

        auto error = [](DatagramState const& state)
        {
            THROW_ERROR_DATAGRAM("Error while trying to get slave register.", state);
        };

        link.addDatagram(Command::FPRD, createAddress(slave_address, reg_address), nullptr, size, process, error);
        link.processDatagrams();
    }

    void writeRegister(AbstractLink& link, uint16_t slave_address, uint16_t reg_address, void const* value, uint16_t size)
    {
        auto process = [](DatagramHeader const*, uint8_t const*, uint16_t wkc)
        {
            if (wkc != 1)
            {
                return DatagramState::INVALID_WKC;
            }
            return DatagramState::OK;
        };

        auto error = [](DatagramState const& state)
        {
            THROW_ERROR_DATAGRAM("Error while trying to set slave register.", state);
        };

        link.addDatagram(Command::FPWR, createAddress(slave_address, reg_address), value, size, process, error);
        link.processDatagrams();
    }
}
