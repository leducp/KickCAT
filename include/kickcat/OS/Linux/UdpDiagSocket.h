#ifndef KICKAT_LINUX_UDP_DIAG_SOCKET_H
#define KICKAT_LINUX_UDP_DIAG_SOCKET_H

#include "kickcat/AbstractDiagSocket.h"

#include <vector>
#include <arpa/inet.h>

namespace kickcat
{
    class UdpDiagSocket : public AbstractDiagSocket
    {
    public:
        UdpDiagSocket();
        virtual ~UdpDiagSocket()
        {
            close();
        }

        void open() override;
        void close() noexcept override;
        std::tuple<int32_t, uint16_t> recv(uint8_t* frame, int32_t frame_size) override;
        int32_t sendTo(uint8_t const* frame, int32_t frame_size, uint16_t id) override;

    private:
        int fd_{-1};

        std::vector<struct sockaddr_in> requests_;
    };
}

#endif
