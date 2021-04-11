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
        int32_t read(uint8_t* frame, int32_t frame_size) override;
        int32_t write(uint8_t const* frame, int32_t frame_size) override;

    private:
        int fd_;
    };
}

#endif
