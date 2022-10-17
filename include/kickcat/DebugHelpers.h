#ifndef KICKCAT_DEBUGHELPERS_H
#define KICKCAT_DEBUGHELPERS_H

#include "Link.h"

namespace kickcat
{

    template<typename T>
    void sendGetRegister(Link& link, uint16_t const& slave_address, uint16_t const& reg_address, T& value_read)
    {
        auto process = [&value_read](DatagramHeader const*, uint8_t const* data, uint16_t wkc)
        {
            if (wkc != 1)
            {
                return DatagramState::INVALID_WKC;
            }

            value_read = *reinterpret_cast<T const*>(data);
            return DatagramState::OK;
        };

        auto error = [](DatagramState const& state)
        {
            THROW_ERROR_DATAGRAM("Send get register", state);
        };

        link.addDatagram(Command::FPRD, createAddress(slave_address, reg_address), nullptr, process, error);
        link.processDatagrams();
    }

    template<typename T>
    void sendWriteRegister(Link& link, uint16_t const& slave_address, uint16_t const& reg_address, T value_write)
    {
        auto process = [&value_write](DatagramHeader const*, uint8_t const*, uint16_t wkc)
        {
            if (wkc != 1)
            {
                return DatagramState::INVALID_WKC;
            }
            return DatagramState::OK;
        };

        auto error = [](DatagramState const& state)
        {
            THROW_ERROR_DATAGRAM("Send write register", state);
        };

        link.addDatagram(Command::FPWR, createAddress(slave_address, reg_address), value_write, process, error);
        link.processDatagrams();
    }
}

#endif
