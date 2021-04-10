#ifndef KICKCAT_ABSTRACT_SOCKET_H
#define KICKCAT_ABSTRACT_SOCKET_H

#include "Error.h"

namespace kickcat
{
    class AbstractSocket
    {
    public:
        AbstractSocket() = default;
        virtual ~AbstractSocket() = default;

        virtual Error open(std::string const& interface) = 0;
        virtual Error close() = 0;
        virtual Error read(uint8_t* frame, int32_t frame_size) = 0;
        virtual Error write(uint8_t const* frame, int32_t frame_size) = 0;
    };
}

#endif
