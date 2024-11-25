#ifndef KICKAT_WINDOWS_SOCKET_H
#define KICKAT_WINDOWS_SOCKET_H

#include <vector>

#include "kickcat/AbstractSocket.h"

namespace kickcat
{
    class Socket : public AbstractSocket
    {
    public:
        Socket(nanoseconds polling_period = 20us);
        virtual ~Socket()
        {
            close();
        }

        void open(std::string const& interface) override;
        void setTimeout(nanoseconds timeout) override;
        void close() noexcept override;
        int32_t read(void* frame, int32_t frame_size) override;
        int32_t write(void const* frame, int32_t frame_size) override;

    private:
        void* fd_;
        std::vector<char> error_;
        nanoseconds timeout_;
        nanoseconds polling_period_;
    };
}

#endif
