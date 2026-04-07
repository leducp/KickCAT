#ifndef KICKMSG_NODE_H
#define KICKMSG_NODE_H

#include <string>
#include <vector>

#include "Region.h"
#include "Publisher.h"
#include "Subscriber.h"

namespace kickmsg
{

    struct BroadcastHandle
    {
        Publisher  pub;
        Subscriber sub;
    };

    /// High-level messaging node: manages shared-memory regions and provides
    /// Publisher/Subscriber handles for topic-centric communication.
    ///
    /// Lifetime: the Node owns the underlying shared-memory mappings. All
    /// Publisher, Subscriber, and BroadcastHandle objects returned by this
    /// Node hold raw pointers into the mapped memory. They MUST NOT outlive
    /// the Node that created them — destroying the Node unmaps the memory
    /// and silently invalidates all outstanding handles.
    class Node
    {
    public:
        explicit Node(std::string name, std::string prefix = "kickmsg");

        // --- PubSub (topic-centric, 1-to-N by convention) ---
        Publisher  advertise(char const* topic, ChannelConfig const& cfg = {});
        Subscriber subscribe(char const* topic);

        // --- Broadcast (N-to-N shared channel) ---
        BroadcastHandle join_broadcast(char const* channel, ChannelConfig const& cfg = {});

        // --- Mailbox (N-to-1, max_subscribers=1) ---
        Subscriber create_mailbox(char const* tag, ChannelConfig const& cfg = {});
        Publisher  open_mailbox(char const* owner_node, char const* tag);

        std::string const& name()   const { return name_; }
        std::string const& prefix() const { return prefix_; }

    private:
        std::string make_topic_name(char const* topic) const;
        std::string make_broadcast_name(char const* channel) const;
        std::string make_mailbox_name(char const* owner, char const* tag) const;

        std::string name_;
        std::string prefix_;
        std::vector<SharedRegion> regions_;
    };

} // namespace kickmsg

#endif // KICKMSG_NODE_H
