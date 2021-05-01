#ifndef KICKCAT_ABSTRACT_SOCKET_H
#define KICKCAT_ABSTRACT_SOCKET_H

#include <string>
#include <system_error>

namespace kickcat
{
    class AbstractSocket
    {
    public:
        AbstractSocket() = default;
        virtual ~AbstractSocket() = default;

        virtual void open(std::string const& interface) = 0;
        virtual void close() = 0;
        virtual int32_t read(uint8_t* frame, int32_t frame_size) = 0;
        virtual int32_t write(uint8_t const* frame, int32_t frame_size) = 0;
    };
}

#endif
