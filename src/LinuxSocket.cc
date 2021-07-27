#include <unistd.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <linux/if_packet.h>
#include <linux/ethtool.h>
#include <linux/sockios.h>

#include <cstring>

#include "LinuxSocket.h"
#include "protocol.h"
#include "Time.h"

namespace kickcat
{
    LinuxSocket::LinuxSocket(microseconds rx_coalescing)
        : AbstractSocket()
        , fd_{-1}
        , rx_coalescing_{rx_coalescing}
    {

    }

    void LinuxSocket::open(std::string const& interface, microseconds requested_timeout)
    {
        // RAW socket with EtherCAT type
        fd_ = socket(PF_PACKET, SOCK_RAW, ETH_ETHERCAT_TYPE);
        if (fd_ < 0)
        {
            THROW_SYSTEM_ERROR("socket()");
        }

        // Non-blocking mode
        struct timeval timeout{0, requested_timeout.count()};

        int rc = setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        if (rc < 0)
        {
            THROW_SYSTEM_ERROR("setsockopt(SO_RCVTIMEO)");
        }

        rc = setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        if (rc < 0)
        {
            THROW_SYSTEM_ERROR("setsockopt(SO_SNDTIMEO)");
        }


        constexpr int buffer_size = ETH_MAX_SIZE * 256; // max 256 frames on the wire
        rc = setsockopt(fd_, SOL_SOCKET, SO_SNDBUF, &buffer_size, sizeof(buffer_size));
        if (rc < 0)
        {
            THROW_SYSTEM_ERROR("setsockopt(SO_SNDBUF)");
        }

        rc = setsockopt(fd_, SOL_SOCKET, SO_RCVBUF, &buffer_size, sizeof(buffer_size));
        if (rc < 0)
        {
            THROW_SYSTEM_ERROR("setsockopt(SO_RCVBUF)");
        }

        // EtherCAT frames shall not be routed to another network, but only on the dedicated interface
        constexpr int dont_route = 1;
        rc = setsockopt(fd_, SOL_SOCKET, SO_DONTROUTE, &dont_route, sizeof(dont_route));
        if (rc < 0)
        {
            THROW_SYSTEM_ERROR("setsockopt(SO_DONTROUTE)");
        }

        // Connect socket to interface and configure interface for EtherCAT use (promiscious, broadcast)
        struct ifreq ifr;
        std::strncpy(ifr.ifr_name, interface.c_str(), sizeof(ifr.ifr_name)-1);
        rc = ioctl(fd_, SIOCGIFINDEX, &ifr);
        if (rc < 0)
        {
            THROW_SYSTEM_ERROR("ioctl(SIOCGIFINDEX)");
        }
        int interface_index = ifr.ifr_ifindex;

        ifr.ifr_flags = 0;
        rc = ioctl(fd_, SIOCGIFFLAGS, &ifr);
        if (rc < 0)
        {
            THROW_SYSTEM_ERROR("ioctl(SIOCGIFFLAGS)");
        }

        ifr.ifr_flags = ifr.ifr_flags | IFF_PROMISC | IFF_BROADCAST;
        rc = ioctl(fd_, SIOCSIFFLAGS, &ifr);
        if (rc < 0)
        {
            THROW_SYSTEM_ERROR("ioctl(SIOCSIFFLAGS)");
        }

        // apply coalescing if asked
        if (rx_coalescing_ >= 0us)
        {
            struct ethtool_coalesce ecoal;
            ecoal.cmd = ETHTOOL_GCOALESCE;
            ifr.ifr_data = (char*)&ecoal;
            rc = ioctl(fd_, SIOCETHTOOL, &ifr);
            if (rc < 0)
            {
                THROW_SYSTEM_ERROR("ioctl(SIOCETHTOOL - ETHTOOL_GCOALESCE)");
            }
            DEBUG_PRINT("old rx-usecs value %u\n", ecoal.rx_coalesce_usecs);

            ecoal.cmd = ETHTOOL_SCOALESCE;
            ecoal.rx_coalesce_usecs = static_cast<unsigned int>(rx_coalescing_.count());
            rc = ioctl(fd_, SIOCETHTOOL, &ifr);
            if (rc < 0)
            {
                THROW_SYSTEM_ERROR("ioctl(SIOCETHTOOL - ETHTOOL_GCOALESCE)");
            }
            DEBUG_PRINT("applied rx-usecs value %u\n", ecoal.rx_coalesce_usecs);
        }


        struct sockaddr_ll link_layer;
        link_layer.sll_family = AF_PACKET;
        link_layer.sll_ifindex = interface_index;
        link_layer.sll_protocol = ETH_ETHERCAT_TYPE;
        rc = bind(fd_, (struct sockaddr *)&link_layer, sizeof(link_layer));
        if (rc < 0)
        {
            THROW_SYSTEM_ERROR("bind()");
        }
    }

    void LinuxSocket::close() noexcept
    {
        if (fd_ == -1)
        {
            return;
        }

        int rc = ::close(fd_);
        if (rc < 0)
        {
            perror(LOCATION ": close()"); // we cannot throw here - at least trace the error
        }
        fd_ = -1;
    }

    int32_t LinuxSocket::read(uint8_t* frame, int32_t frame_size)
    {
        return ::recv(fd_, frame, frame_size, 0);
    }

    int32_t LinuxSocket::write(uint8_t const* frame, int32_t frame_size)
    {
        return ::send(fd_, frame, frame_size, 0);
    }
}
