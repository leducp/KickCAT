#ifndef KICKCAT_TAP_SOCKET_H
#define KICKCAT_TAP_SOCKET_H

#include <memory>
#include <string>

#include "kickcat/AbstractSocket.h"
#include "kickmsg/Region.h"
#include "kickmsg/Publisher.h"
#include "kickmsg/Subscriber.h"

namespace kickcat
{
    class TapSocket final : public AbstractSocket
    {
    public:
        static constexpr std::size_t MAX_FRAME_SIZE = 1522;

        TapSocket(bool init = false);
        virtual ~TapSocket();

        void open(std::string const& interface) override;
        void close() noexcept override;
        /// Set the timeout to receive a frame. If negative, the timeout is infinite (blocking call).
        void setTimeout(nanoseconds timeout) override;

        int32_t read(void* frame, int32_t frame_size) override;
        int32_t write(void const* frame, int32_t frame_size) override;

    private:
        static kickmsg::ChannelConfig defaultConfig();

        bool init_;
        nanoseconds timeout_;

        kickmsg::SharedRegion region_in_;
        kickmsg::SharedRegion region_out_;

        std::unique_ptr<kickmsg::Publisher>  publisher_;
        std::unique_ptr<kickmsg::Subscriber> subscriber_;
    };
}

#endif
