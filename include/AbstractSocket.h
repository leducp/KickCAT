#ifndef KICKCAT_ABSTRACT_SOCKET_H
#define KICKCAT_ABSTRACT_SOCKET_H

#include "Error.h"

namespace kickcat
{
    // MAC addresses are not used by EtherCAT but set them helps the debug easier when following a network trace.
    constexpr uint8_t PRIMARY_IF_MAC[6]   = { 0x02, 0x02, 0x02, 0x02, 0x02, 0x02 };
    constexpr uint8_t SECONDARY_IF_MAC[6] = { 0x0A, 0x0A, 0x0A, 0x0A, 0x0A, 0x0A };

    class AbstractSocket
    {

    public:
        AbstractSocket() = default;
        virtual ~AbstractSocket() = default;

        virtual Error open(std::string const& interface) = 0;
        virtual Error close() = 0;
        virtual Error read(std::vector<uint8_t>& datagram) = 0;
        virtual Error write(std::vector<uint8_t> const& datagram) = 0;
    };
}

#endif
