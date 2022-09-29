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

        auto error = [](DatagramState const&)
        {
            THROW_ERROR("Error while trying to get slave register.");
        };

        link.addDatagram(Command::FPRD, createAddress(slave_address, reg_address), nullptr, process, error);
        link.processDatagrams();
    }

    void sendGetRegister(Link& link, uint16_t slave_address, uint16_t reg_address, uint16_t& value_read)
    {
        sendGetRegister<uint16_t>(link, slave_address, reg_address, value_read);
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

        auto error = [](DatagramState const&)
        {
            THROW_ERROR("Error while trying to set slave register.");
        };

        link.addDatagram(Command::FPWR, createAddress(slave_address, reg_address), value_write, process, error);
        link.processDatagrams();
    }

    void sendWriteRegister(Link& link, uint16_t const& slave_address, uint16_t const& reg_address, uint16_t const& value_write)
    {
        sendWriteRegister<uint16_t>(link, slave_address, reg_address, value_write);
    }
}

#endif
