/// @file hello_broadcast.cc
/// @brief KickMsg N-to-N broadcast with multiple nodes.
///
/// Four nodes join a "chat" broadcast channel. Each node can both
/// publish and receive. Demonstrates:
///   - join_broadcast(): returns both a Publisher and Subscriber
///   - Multiple concurrent participants on the same channel
///   - Each node sees messages from all other nodes (not its own)

#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <kickmsg/Node.h>

using namespace kickcat;

struct ChatMessage
{
    char sender[16];
    char text[112];
};

static void chat_node(char const* name, char const* message,
                      std::atomic<int>& ready, int total_nodes)
{
    kickmsg::channel::Config cfg;
    cfg.max_subscribers   = 8;
    cfg.sub_ring_capacity = 32;
    cfg.pool_size         = 64;
    cfg.max_payload_size  = sizeof(ChatMessage);

    kickmsg::Node node(name, "demo");
    auto [pub, sub] = node.join_broadcast("chat", cfg);

    // Signal ready and wait for all nodes
    ready.fetch_add(1, std::memory_order_release);
    while (ready.load(std::memory_order_acquire) < total_nodes)
    {
        kickcat::sleep(1ms);
    }

    // Send our message
    ChatMessage msg{};
    std::strncpy(msg.sender, name, sizeof(msg.sender) - 1);
    std::strncpy(msg.text, message, sizeof(msg.text) - 1);
    pub.send(&msg, sizeof(msg));

    // Give others time to publish
    kickcat::sleep(10ms);

    // Receive messages from others
    while (auto sample = sub.try_receive())
    {
        ChatMessage received;
        std::memcpy(&received, sample->data(), sizeof(received));

        // Skip our own messages
        if (std::strcmp(received.sender, name) == 0)
        {
            continue;
        }

        std::cout << "[" << name << " sees] " << received.sender
                  << ": " << received.text << "\n";
    }
}

int main()
{
    kickmsg::SharedMemory::unlink("/demo_broadcast_chat");

    // Pre-create the channel so all nodes can join without racing create_or_open
    kickmsg::channel::Config cfg;
    cfg.max_subscribers   = 8;
    cfg.sub_ring_capacity = 32;
    cfg.pool_size         = 64;
    cfg.max_payload_size  = sizeof(ChatMessage);

    auto setup_region = kickmsg::SharedRegion::create(
        "/demo_broadcast_chat", kickmsg::channel::Broadcast, cfg, "setup");

    std::atomic<int> ready{0};
    constexpr int NUM_NODES = 4;

    std::vector<std::thread> threads;
    threads.emplace_back(chat_node, "alice",   "Hello everyone!", std::ref(ready), NUM_NODES);
    threads.emplace_back(chat_node, "bob",     "Hey alice!",      std::ref(ready), NUM_NODES);
    threads.emplace_back(chat_node, "charlie", "Good morning!",   std::ref(ready), NUM_NODES);
    threads.emplace_back(chat_node, "dave",    "Hi all :)",       std::ref(ready), NUM_NODES);

    for (auto& t : threads)
    {
        t.join();
    }

    kickmsg::SharedMemory::unlink("/demo_broadcast_chat");
    std::cout << "Done.\n";
    return 0;
}
