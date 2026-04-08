/// @file hello_pubsub.cc
/// @brief Minimal KickMsg publish/subscribe example.
///
/// Demonstrates basic shared-memory pub/sub with copy-based receive:
///   1. Create a shared region
///   2. Publish messages from a Publisher
///   3. Receive copies with a Subscriber (SampleRef)
///
/// Single-process for simplicity; in production, publisher and subscriber
/// typically live in separate processes sharing the same named region.

#include <cstdint>
#include <cstring>
#include <iostream>

#include <kickmsg/Publisher.h>
#include <kickmsg/Subscriber.h>

int main()
{
    char const* SHM_NAME = "/kickmsg_hello_pubsub";
    kickmsg::SharedMemory::unlink(SHM_NAME);

    // Configure the channel
    kickmsg::ChannelConfig cfg;
    cfg.max_subscribers   = 2;
    cfg.sub_ring_capacity = 8;
    cfg.pool_size         = 16;
    cfg.max_payload_size  = 128;

    // Create the shared region
    auto region = kickmsg::SharedRegion::create(
        SHM_NAME, kickmsg::channel::PubSub, cfg, "hello_example");

    // Attach a subscriber, then a publisher
    kickmsg::Subscriber sub(region);
    kickmsg::Publisher  pub(region);

    // Send a few messages
    for (uint32_t i = 0; i < 5; ++i)
    {
        if (pub.send(&i, sizeof(i)) < 0)
        {
            std::cerr << "Failed to send message " << i << "\n";
        }
    }

    // Receive them (copy-based: data is copied into subscriber's buffer)
    for (uint32_t i = 0; i < 5; ++i)
    {
        auto sample = sub.try_receive();
        if (!sample)
        {
            std::cerr << "Missing message " << i << "\n";
            continue;
        }

        uint32_t value = 0;
        std::memcpy(&value, sample->data(), sizeof(value));
        std::cout << "Received: " << value << "\n";
    }

    // Clean up
    region.unlink();
    std::cout << "Done.\n";
    return 0;
}
