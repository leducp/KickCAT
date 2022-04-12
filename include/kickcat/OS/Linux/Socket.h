#ifndef KICKAT_LINUX_SOCKET_H
#define KICKAT_LINUX_SOCKET_H

#include "kickcat/AbstractSocket.h"

namespace kickcat
{
    class Socket : public AbstractSocket
    {
    public:
        Socket(microseconds rx_coalescing = -1us, microseconds polling_period = 20us);
        virtual ~Socket()
        {
            close();
        }

        void open(std::string const& interface, microseconds timeout) override;
        void setTimeout(microseconds timeout) override;
        void close() noexcept override;
        int32_t read(uint8_t* frame, int32_t frame_size) override;
        int32_t write(uint8_t const* frame, int32_t frame_size) override;

    private:
        int fd_{-1};
        microseconds rx_coalescing_;
        microseconds timeout_;
        microseconds polling_period_;
    };
}

#endif
