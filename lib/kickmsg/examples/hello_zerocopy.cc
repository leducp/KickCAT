/// @file hello_zerocopy.cc
/// @brief KickMsg zero-copy receive via the Node API.
///
/// Demonstrates SampleView: a zero-copy handle that points directly into
/// shared memory. The slot stays pinned (refcount > 0) while the view
/// is alive, preventing reuse. Ideal for large payloads (camera frames,
/// point clouds) where memcpy overhead matters.

#include <cstdint>
#include <cstring>
#include <iostream>

#include <kickmsg/Node.h>

using namespace kickcat;

struct ImageHeader
{
    uint32_t width;
    uint32_t height;
    uint32_t channels;
    uint32_t frame_id;
    // In a real application, pixel data follows this header.
};

int main()
{
    kickmsg::SharedMemory::unlink("/demo_camera");

    kickmsg::channel::Config cfg;
    cfg.max_subscribers   = 4;
    cfg.sub_ring_capacity = 8;
    cfg.pool_size         = 16;
    cfg.max_payload_size  = sizeof(ImageHeader) + 1024; // header + small payload

    // Camera node publishes frames
    kickmsg::Node camera("camera", "demo");
    auto pub = camera.advertise("frames", cfg);

    // Viewer node subscribes with zero-copy
    kickmsg::Node viewer("viewer", "demo");
    auto sub = viewer.subscribe("frames");

    // Publish a few "frames"
    for (uint32_t i = 0; i < 3; ++i)
    {
        void* ptr = pub.allocate(sizeof(ImageHeader));
        if (ptr == nullptr)
        {
            std::cerr << "Pool exhausted at frame " << i << "\n";
            continue;
        }

        ImageHeader hdr{640, 480, 3, i};
        std::memcpy(ptr, &hdr, sizeof(hdr));
        pub.publish();

        std::cout << "Published frame " << i << " (640x480x3)\n";
    }

    // Zero-copy receive: view points directly into shared memory
    while (auto view = sub.try_receive_view())
    {
        auto const* hdr = static_cast<ImageHeader const*>(view->data());
        std::cout << "Received frame " << hdr->frame_id
                  << " (" << hdr->width << "x" << hdr->height
                  << "x" << hdr->channels << ")"
                  << " — zero-copy, " << view->len() << " bytes pinned\n";

        // The slot remains pinned while 'view' is alive.
        // No memcpy needed — read directly from shared memory.
    }
    // All views destroyed here -> slots return to free pool

    kickmsg::SharedMemory::unlink("/demo_camera");
    std::cout << "Done.\n";
    return 0;
}
