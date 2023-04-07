#ifndef KICKCAT_ABSTRACT_SOCKET_H
#define KICKCAT_ABSTRACT_SOCKET_H

#include <string>
#include <chrono>

#include "Error.h"

namespace kickcat
{
    using namespace std::chrono;

    class AbstractSocket
    {
    public:
        AbstractSocket() = default;
        virtual ~AbstractSocket() = default;

        virtual void open(std::string const& interface) = 0;

        /// Set the timeout to receive a frame. If negative, the timeout is infinite (blocking call).
        virtual void setTimeout(nanoseconds timeout) = 0;
        virtual void close() noexcept = 0;
        virtual int32_t read(uint8_t* frame, int32_t frame_size) = 0;
        virtual int32_t write(uint8_t const* frame, int32_t frame_size) = 0;
    };
}

#endif
