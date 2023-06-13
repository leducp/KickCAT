#ifndef KICKAT_LINUX_SOCKET_H
#define KICKAT_LINUX_SOCKET_H

#include "kickcat/AbstractSocket.h"

namespace kickcat
{
    class Socket : public AbstractSocket
    {
    public:
        Socket(nanoseconds coalescing = -1us, nanoseconds polling_period = 20us);
        virtual ~Socket()
        {
            close();
        }

        void open(std::string const& interface) override;
        void setTimeout(nanoseconds timeout) override;
        void close() noexcept override;
        int32_t read(uint8_t* frame, int32_t frame_size) override;
        int32_t write(uint8_t const* frame, int32_t frame_size) override;

    private:
        int fd_{-1};
        nanoseconds coalescing_;
        nanoseconds timeout_;
        nanoseconds polling_period_;

        int flags_{0};
    };
}

#endif
