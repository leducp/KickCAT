#ifndef KICKCAT_ABSTRACT_SOCKET_H
#define KICKCAT_ABSTRACT_SOCKET_H

#include <string>
#include <chrono>
#include <vector>

#include "kickcat/Error.h"

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
        virtual int32_t read(void* frame, int32_t frame_size) = 0;
        virtual int32_t write(void const* frame, int32_t frame_size) = 0;
    };


    struct NetworkInterface
    {
        std::string name;
        std::string description;

        std::string format() const;
    };
    std::vector<NetworkInterface> listInterfaces();
}

#endif
