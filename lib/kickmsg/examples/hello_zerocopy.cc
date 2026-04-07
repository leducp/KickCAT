/// @file hello_zerocopy.cc
/// @brief KickMsg zero-copy receive example.
///
/// Demonstrates the SampleView API for zero-copy message consumption:
///   - SampleView holds a direct pointer into shared memory
///   - A refcount pin keeps the slot alive while the view exists
///   - The slot is released automatically when SampleView is destroyed
///
/// Zero-copy is ideal for large payloads (camera frames, point clouds)
/// where memcpy overhead matters. For small, high-frequency data,
/// prefer try_receive() which copies into a local buffer.

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string_view>

#include <kickmsg/Publisher.h>
#include <kickmsg/Subscriber.h>

int main()
{
    char const* SHM_NAME = "/kickmsg_hello_zerocopy";
    kickmsg::SharedMemory::unlink(SHM_NAME);

    kickmsg::ChannelConfig cfg;
    cfg.max_subscribers   = 2;
    cfg.sub_ring_capacity = 8;
    cfg.pool_size         = 16;
    cfg.max_payload_size  = 1024;

    auto region = kickmsg::SharedRegion::create(
        SHM_NAME, kickmsg::ChannelType::PubSub, cfg, "zerocopy_example");

    kickmsg::Subscriber sub(region);
    kickmsg::Publisher  pub(region);

    // Publish a large-ish payload
    char const* message = "Hello from shared memory! No copies on the receive path.";
    if (pub.send(message, std::strlen(message)) < 0)
    {
        std::cerr << "Failed to send message\n";
    }

    // Zero-copy receive: view points directly into shared memory
    auto view = sub.try_receive_view();
    if (view)
    {
        std::cout << "Zero-copy receive (" << view->len() << " bytes): "
                  << std::string_view(static_cast<char const*>(view->data()), view->len())
                  << "\n";

        // The shared-memory slot stays pinned while 'view' is alive.
        // You can safely read view->data() until 'view' goes out of scope.
    }
    // view is destroyed here -> refcount drops, slot returns to the free pool

    // Verify the slot was released: we can send another message reusing it
    uint32_t seq = 42;
    if (pub.send(&seq, sizeof(seq)) < 0)
    {
        std::cerr << "Failed to send seq\n";
    }

    auto view2 = sub.try_receive_view();
    if (view2)
    {
        uint32_t got = 0;
        std::memcpy(&got, view2->data(), sizeof(got));
        std::cout << "Second message (after slot reuse): " << got << "\n";
    }

    region.unlink();
    std::cout << "Done.\n";
    return 0;
}
