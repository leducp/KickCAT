#ifndef KICKCAT_ABSTRACT_SOCKET_H
#define KICKCAT_ABSTRACT_SOCKET_H

#include <string>
#include <chrono>
#include <vector>

namespace kickcat
{
    using namespace std::chrono;

    class AbstractSocket
    {
    public:
        AbstractSocket() = default;
        virtual ~AbstractSocket() = default;

        virtual void open(std::string const& interface) = 0;

        /// Set the timeout to receive a frame. If negative, the timeout is infinite (blocking call).
        virtual void setTimeout(nanoseconds timeout) = 0;
        virtual void close() noexcept = 0;

        /// \param  frame       frame where the data will be stored
        /// \param  frame_size  max size of the frame to read
        /// \return Number of bytes read or a negative errno code otherwise
        virtual int32_t read(void* frame, int32_t frame_size) = 0;

        /// \param  frame       frame to write on the network
        /// \param  frame_size  size of the frame to write on the network
        /// \return Number of bytes written or a negative errno code otherwise
        virtual int32_t write(void const* frame, int32_t frame_size) = 0;
    };


    struct NetworkInterface
    {
        std::string name;
        std::string description;

        std::string format() const;
    };
    std::vector<NetworkInterface> listInterfaces();
}

#endif
