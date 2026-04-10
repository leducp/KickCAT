/// @file hello_lowlevel.cc
/// @brief KickMsg low-level API example using SharedRegion directly.
///
/// Demonstrates the raw API without the Node abstraction:
///   1. Create a SharedRegion with explicit config
///   2. Attach Publisher and Subscriber directly
///   3. Send and receive messages
///
/// Use this when you need full control over the shared-memory region
/// (custom naming, config tuning, multi-process open/create patterns).
/// For most use cases, prefer the Node API (see hello_pubsub.cc).

#include <cstdint>
#include <cstring>
#include <iostream>

#include <kickmsg/Publisher.h>
#include <kickmsg/Subscriber.h>

int main()
{
    char const* SHM_NAME = "/kickmsg_hello_lowlevel";
    kickmsg::SharedMemory::unlink(SHM_NAME);

    // Configure the channel
    kickmsg::channel::Config cfg;
    cfg.max_subscribers   = 2;
    cfg.sub_ring_capacity = 8;
    cfg.pool_size         = 16;
    cfg.max_payload_size  = 128;

    // Create the shared region
    auto region = kickmsg::SharedRegion::create(
        SHM_NAME, kickmsg::channel::PubSub, cfg, "lowlevel_example");

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
    while (auto sample = sub.try_receive())
    {
        uint32_t value = 0;
        std::memcpy(&value, sample->data(), sizeof(value));
        std::cout << "Received: " << value << "\n";
    }

    // In multi-process scenarios:
    //   Process A: SharedRegion::create(name, ...) or create_or_open(name, ...)
    //   Process B: SharedRegion::open(name) or create_or_open(name, ...)
    // Both processes map the same /dev/shm/ file.

    region.unlink();
    std::cout << "Done.\n";
    return 0;
}
