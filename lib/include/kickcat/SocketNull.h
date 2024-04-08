#ifndef KICKCAT_SOCKET_NULL_H
#define KICKCAT_SOCKET_NULL_H

#include "kickcat/Error.h"
#include "AbstractSocket.h"

namespace kickcat
{
    /// \brief Void socket represents an absent interface, allowing to use the link without redundancy.
    class SocketNull : public AbstractSocket
    {
    public:
        SocketNull() = default;
        virtual ~SocketNull() = default;

        void open(std::string const& ) override {}
        void setTimeout(nanoseconds ) override {}
        void close() noexcept override {}
        int32_t read(void* , int32_t ) override { return 0;}
        int32_t write(void const* , int32_t frame_size) override { return frame_size;};
    };
}

#endif
