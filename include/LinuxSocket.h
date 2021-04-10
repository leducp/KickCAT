#ifndef KICKAT_LINUX_SOCKET_H
#define KICKAT_LINUX_SOCKET_H

#include "AbstractSocket.h"

namespace kickcat
{
    class LinuxSocket : public AbstractSocket
    {
    public:
        LinuxSocket() = default;
        virtual ~LinuxSocket() = default;

        Error open(std::string const& interface) override;
        Error close() override;
        Error read(std::vector<uint8_t>& datagram) override;
        Error write(std::vector<uint8_t> const& datagram) override;

    private:
        int fd_;
    };
}

#endif
