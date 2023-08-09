#include "OS/Linux/UdpDiagSocket.h"
#include "kickcat/protocol.h"

#include <unistd.h>
#include <cstring>
#include <tuple>

namespace kickcat
{
    UdpDiagSocket::UdpDiagSocket()
    {
        requests_.resize(mailbox::GATEWAY_MAX_REQUEST);
    }

    void UdpDiagSocket::open()
    {
        fd_ = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (fd_ < 0)
        {
            THROW_SYSTEM_ERROR("socket()");
        }

        // Bind socket to listen to requests
        struct sockaddr_in addr;
        std::memset(&addr, 0, sizeof(addr));

        addr.sin_family      = AF_INET;         // IPv4
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = hton<uint16_t>(0x88A4); // Port is defined in ETG 8200

        if (::bind(fd_, (struct sockaddr const*)&addr, sizeof(addr)) < 0)
        {
            THROW_SYSTEM_ERROR("bind()");
        }
    }

    void UdpDiagSocket::close() noexcept
    {
        if (fd_ == -1)
        {
            return;
        }

        int rc = ::close(fd_);
        if (rc < 0)
        {
            perror(LOCATION(": close()")); // we cannot throw here - at least trace the error
        }
        fd_ = -1;
    }

    std::tuple<int32_t, uint16_t> UdpDiagSocket::recv(uint8_t* frame, int32_t frame_size)
    {
        socklen_t origin_size = sizeof(struct sockaddr_in);
        ssize_t rec = ::recvfrom(fd_, frame, frame_size, MSG_DONTWAIT, (struct sockaddr*)&requests_[index_], &origin_size);
        if (rec <= 0)
        {
            return std::make_tuple(-1, 0);
        }

        // Read successful: compute next index and return the current one
        uint16_t current_index = index_;
        nextIndex();
        return std::make_tuple(rec, current_index);
    }

    int32_t UdpDiagSocket::sendTo(uint8_t const* frame, int32_t frame_size, uint16_t id)
    {
        int32_t index = id & (mailbox::GATEWAY_MAX_REQUEST - 1);
        ssize_t sent = ::sendto(fd_, frame, frame_size, MSG_DONTWAIT, (struct sockaddr*)&requests_[index], sizeof(struct sockaddr_in));
        if (sent < 0)
        {
            return -1;
        }

        return static_cast<int32_t>(sent);
    }
}
