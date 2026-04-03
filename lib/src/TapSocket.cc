#include <cstring>

#include "kickcat/TapSocket.h"
#include "kickcat/Error.h"

namespace kickcat
{
// LCOV_EXCL_START
    TapSocket::TapSocket(bool init)
        : init_{init}
    {
        setTimeout(0ns);
    }

    TapSocket::~TapSocket()
    {
        close();
    }

    kickmsg::RingConfig TapSocket::defaultConfig()
    {
        kickmsg::RingConfig cfg;
        cfg.max_subscribers   = 1;
        cfg.sub_ring_capacity = 64;
        cfg.pool_size         = 128;
        cfg.max_payload_size  = MAX_FRAME_SIZE;
        return cfg;
    }

    void TapSocket::open(std::string const& interface)
    {
        auto cfg = defaultConfig();
        std::string name_a2b = "/" + interface + "_a2b";
        std::string name_b2a = "/" + interface + "_b2a";

        if (init_)
        {
            // Server creates both regions and takes side A (publish a2b, subscribe b2a)
            region_out_ = kickmsg::SharedRegion::create(name_a2b.c_str(), kickmsg::ChannelType::PubSub, cfg, "kickcat");
            region_in_  = kickmsg::SharedRegion::create(name_b2a.c_str(), kickmsg::ChannelType::PubSub, cfg, "kickcat");
        }
        else
        {
            // Client opens both regions and takes side B (publish b2a, subscribe a2b)
            region_in_  = kickmsg::SharedRegion::open(name_a2b.c_str());
            region_out_ = kickmsg::SharedRegion::open(name_b2a.c_str());
        }

        publisher_  = std::make_unique<kickmsg::Publisher>(region_out_);
        subscriber_ = std::make_unique<kickmsg::Subscriber>(region_in_);
    }

    void TapSocket::close() noexcept
    {
        subscriber_.reset();
        publisher_.reset();
    }

    void TapSocket::setTimeout(nanoseconds timeout)
    {
        timeout_ = timeout;
    }

    int32_t TapSocket::read(void* frame, int32_t frame_size)
    {
        std::optional<kickmsg::Subscriber::SampleRef> sample;

        if (timeout_ == 0ns)
        {
            sample = subscriber_->try_receive();
        }
        else if (timeout_ < 0ns)
        {
            // Infinite blocking: retry with a large timeout
            while (true)
            {
                sample = subscriber_->receive(1s);
                if (sample)
                {
                    break;
                }
            }
        }
        else
        {
            sample = subscriber_->receive(timeout_);
        }

        if (not sample)
        {
            return -EAGAIN;
        }

        int32_t toCopy = std::min(static_cast<int32_t>(sample->len()), frame_size);
        std::memcpy(frame, sample->data(), toCopy);
        return toCopy;
    }

    int32_t TapSocket::write(void const* frame, int32_t frame_size)
    {
        int32_t toCopy = std::min(static_cast<int32_t>(MAX_FRAME_SIZE), frame_size);
        if (not publisher_->send(frame, static_cast<std::size_t>(toCopy)))
        {
            return -EAGAIN;
        }
        return toCopy;
    }
// LCOV_EXCL_STOP
}
