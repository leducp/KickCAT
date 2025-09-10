#ifndef KICKAT_NUTTX_SOCKET_H
#define KICKAT_NUTTX_SOCKET_H

#include "kickcat/AbstractSocket.h"

namespace kickcat
{
    // Stub socket for NuttX - waiting for a proper implementation
    class Socket : public AbstractSocket
    {
    public:
        Socket() = default;
        virtual ~Socket()
        {
            close();
        }

        void open(std::string const&) override { THROW_ERROR("Not implemented"); }
        void setTimeout(nanoseconds) override {}
        void close() noexcept override {}
        int32_t read(void*, int32_t) override { return -1; }
        int32_t write(void const*, int32_t) override { return -1; }

    private:
    };
}

#endif
