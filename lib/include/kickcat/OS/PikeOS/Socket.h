#ifndef KICKAT_PIKEOS_SOCKET_H
#define KICKAT_PIKEOS_SOCKET_H

#include "kickcat/AbstractSocket.h"
#include "kickcat/OS/PikeOS/ErrorCategory.h"

extern "C"
{
    #include <vm.h>
    #include <vm_io_sbuf.h>
}

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
        int32_t read(uint8_t* frame, int32_t frame_size) override;
        int32_t write(uint8_t const* frame, int32_t frame_size) override;

    private:
        vm_file_desc_t fd_;
        drv_sbuf_desc_t sbuf_;

        nanoseconds timeout_;
        nanoseconds polling_period_;

        int flags_{0};
    };
}

#endif
