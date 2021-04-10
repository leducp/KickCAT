#include <unistd.h>
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <linux/if_packet.h>

#include <cstring>

#include "LinuxSocket.h"
#include "protocol.h"

namespace kickcat
{
    Error LinuxSocket::open(std::string const& interface)
    {
        // RAW socket with EtherCAT type
        fd_ = socket(PF_PACKET, SOCK_RAW, htons(ETH_ETHERCAT_TYPE));
        if (fd_ < 0)
        {
            return EERROR(std::strerror(errno));
        }

        // Non-blocking mode
        struct timeval timeout{0, 1};

        int rc = setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        if (rc < 0)
        {
            return EERROR(std::strerror(errno));
        }

        rc = setsockopt(fd_, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        if (rc < 0)
        {
            return EERROR(std::strerror(errno));
        }

        // EtherCAT frames shall not be routed to another network, but only on the dedicated interface
        int const dont_route = 1;
        rc = setsockopt(fd_, SOL_SOCKET, SO_DONTROUTE, &dont_route, sizeof(dont_route));
        if (rc < 0)
        {
            return EERROR(std::strerror(errno));
        }

        // Connect socket to interface and configure interface for EtherCAT use (promiscious, broadcast)
        struct ifreq ifr;
        std::strncpy(ifr.ifr_name, interface.c_str(), IFNAMSIZ-1);
        rc = ioctl(fd_, SIOCGIFINDEX, &ifr);
        if (rc < 0)
        {
            return EERROR(std::strerror(errno));
        }
        int interface_index = ifr.ifr_ifindex;

        ifr.ifr_flags = 0;
        rc = ioctl(fd_, SIOCGIFFLAGS, &ifr);
        if (rc < 0)
        {
            return EERROR(std::strerror(errno));
        }

        ifr.ifr_flags = ifr.ifr_flags | IFF_PROMISC | IFF_BROADCAST;
        rc = ioctl(fd_, SIOCSIFFLAGS, &ifr);
        if (rc < 0)
        {
            return EERROR(std::strerror(errno));
        }

        struct sockaddr_ll link_layer;
        link_layer.sll_family = AF_PACKET;
        link_layer.sll_ifindex = interface_index;
        link_layer.sll_protocol = htons(ETH_ETHERCAT_TYPE);
        rc = bind(fd_, (struct sockaddr *)&link_layer, sizeof(link_layer));
        if (rc < 0)
        {
            return EERROR(std::strerror(errno));
        }

        return ESUCCESS;
    }

    Error LinuxSocket::close()
    {
        int rc = ::close(fd_);
        if (rc < 0)
        {
            return EERROR(std::strerror(errno));
        }

        return ESUCCESS;
    }

    Error LinuxSocket::read(uint8_t* frame, int32_t frame_size)
    {
        ssize_t rc = ::recv(fd_, frame, frame_size, 0);
        if (rc < 0)
        {
            return EERROR(std::strerror(errno));
        }
        if (frame_size != rc)
        {
            return EERROR("Wrong number of bytes read");
        }

        return ESUCCESS;
    }

    Error LinuxSocket::write(uint8_t const* frame, int32_t frame_size)
    {
        printf("frame size is %d and fd is %d\n", frame_size, fd_);
        ssize_t rc = ::send(fd_, frame, frame_size, 0);
        if (rc < 0)
        {
            printf("wtf %d\n", rc);
            return EERROR(std::strerror(errno));
        }
        if (frame_size != rc)
        {
            return EERROR("Wrong number of bytes written");
        }

        return ESUCCESS;
    }
}
