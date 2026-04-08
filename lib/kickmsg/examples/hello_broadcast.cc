/// @file hello_broadcast.cc
/// @brief KickMsg broadcast + mailbox (request/reply) example.
///
/// Demonstrates the Node high-level API:
///   - join_broadcast(): N-to-N channel where any node can publish and subscribe
///   - create_mailbox():  personal MPSC inbox for receiving replies
///   - open_mailbox():    send a message to another node's mailbox
///
/// Scenario (single process, two threads for simplicity):
///   Node A broadcasts "version?" on the "system" channel.
///   Node B receives it, then replies to Node A's mailbox.
///   Node A reads the reply from its inbox.

#include <cstring>
#include <iostream>
#include <thread>

#include <kickmsg/Node.h>

using namespace kickcat;

int main()
{
    // Clean up any leftovers from a previous run
    kickmsg::SharedMemory::unlink("/demo_broadcast_system");
    kickmsg::SharedMemory::unlink("/demo_nodeA_mbx_reply");

    kickmsg::ChannelConfig cfg;
    cfg.max_subscribers   = 4;
    cfg.sub_ring_capacity = 16;
    cfg.pool_size         = 32;
    cfg.max_payload_size  = 256;

    // --- Node B (responder) runs in a background thread ---
    std::thread responder([&cfg]()
    {
        kickmsg::Node node_b("nodeB", "demo");
        auto [pub, sub] = node_b.join_broadcast("system", cfg);

        auto msg = sub.receive(2s);
        if (!msg)
        {
            std::cerr << "[nodeB] Timed out waiting for broadcast\n";
            return;
        }

        std::string request(static_cast<char const*>(msg->data()), msg->len());
        std::cout << "[nodeB] Received broadcast: '" << request << "'\n";

        // Reply to nodeA's mailbox
        auto reply_pub = node_b.open_mailbox("nodeA", "reply");
        std::string reply = "nodeB v1.2.3";
        if (reply_pub.send(reply.data(), reply.size()) < 0)
        {
            std::cerr << "[nodeB] Failed to send reply\n";
        }
        std::cout << "[nodeB] Sent reply to nodeA's mailbox\n";
    });

    // Give Node B time to join the broadcast channel
    sleep(50ms);

    // --- Node A (requester) ---
    kickmsg::Node node_a("nodeA", "demo");
    auto [pub, sub] = node_a.join_broadcast("system", cfg);
    auto inbox = node_a.create_mailbox("reply", cfg);

    // Broadcast a request
    std::string request = "version?";
    if (pub.send(request.data(), request.size()) < 0)
    {
        std::cerr << "[nodeA] Failed to broadcast request\n";
    }
    std::cout << "[nodeA] Broadcast: '" << request << "'\n";

    // Wait for the reply on our personal mailbox
    auto reply = inbox.receive(2s);
    if (reply)
    {
        std::string answer(static_cast<char const*>(reply->data()), reply->len());
        std::cout << "[nodeA] Got reply: '" << answer << "'\n";
    }
    else
    {
        std::cerr << "[nodeA] Timed out waiting for reply\n";
    }

    responder.join();

    // Clean up shared memory
    kickmsg::SharedMemory::unlink("/demo_broadcast_system");
    kickmsg::SharedMemory::unlink("/demo_nodeA_mbx_reply");

    std::cout << "Done.\n";
    return 0;
}
