#ifndef KICKCAT_OS_LINUX_VIRTUAL_SOCKET_H
#define KICKCAT_OS_LINUX_VIRTUAL_SOCKET_H

#include "kickcat/protocol.h"
#include "kickcat/AbstractSocket.h"
#include "kickcat/OS/Linux/SharedMemory.h"
#include "kickcat/internal/SBufQueue.h"

namespace kickcat
{
    /// \brief A virtual socket that let two process communicates as if they were on a raw socket (tap like interface, without sudo)
    ///        useful to do some testing.
    class VirtualSocket : public AbstractSocket
    {
    public:
        VirtualSocket(nanoseconds polling_period, bool side);
        virtual ~VirtualSocket() = default;

        void open(std::string const& interface) override;
        void setTimeout(nanoseconds timeout) override;
        void close() noexcept override;
        int32_t read(uint8_t* frame, int32_t frame_size) override;
        int32_t write(uint8_t const* frame, int32_t frame_size) override;

        static void createInterface(std::string const& interface);

    private:
        SharedMemory shm_;
        bool side_;
        nanoseconds timeout_;
        nanoseconds polling_period_;

        using VirtualQueue = SBufQueue<EthernetFrame, 256>;
        VirtualQueue tx_;
        VirtualQueue rx_;
    };
}

#endif
