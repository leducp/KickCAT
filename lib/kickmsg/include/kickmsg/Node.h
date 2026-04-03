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

    class Node
    {
    public:
        explicit Node(std::string name, std::string prefix = "kickmsg")
            : name_{std::move(name)}
            , prefix_{std::move(prefix)}
        {
        }

        // --- PubSub (topic-centric, 1-to-N by convention) ---

        Publisher advertise(char const* topic, RingConfig const& cfg = {})
        {
            auto shm_name = make_topic_name(topic);
            regions_.emplace_back(
                SharedRegion::create(shm_name.c_str(), ChannelType::PubSub, cfg, name_.c_str()));
            return Publisher(regions_.back());
        }

        Subscriber subscribe(char const* topic)
        {
            auto shm_name = make_topic_name(topic);
            regions_.emplace_back(SharedRegion::open(shm_name.c_str()));
            return Subscriber(regions_.back());
        }

        // --- Broadcast (N-to-N shared channel) ---

        BroadcastHandle join_broadcast(char const* channel,
                                       RingConfig const& cfg = {})
        {
            auto shm_name = make_broadcast_name(channel);
            regions_.emplace_back(
                SharedRegion::create_or_open(
                    shm_name.c_str(), ChannelType::Broadcast,
                    cfg, name_.c_str()));
            auto& region = regions_.back();
            return BroadcastHandle{Publisher{region}, Subscriber{region}};
        }

        // --- Mailbox (N-to-1, max_subscribers=1) ---

        Subscriber create_mailbox(char const* tag,
                                  RingConfig const& cfg = {})
        {
            RingConfig mbx_cfg = cfg;
            mbx_cfg.max_subscribers = 1;
            auto shm_name = make_mailbox_name(name_.c_str(), tag);
            regions_.emplace_back(
                SharedRegion::create(
                    shm_name.c_str(), ChannelType::PubSub,
                    mbx_cfg, name_.c_str()));
            return Subscriber(regions_.back());
        }

        Publisher open_mailbox(char const* owner_node,
                               char const* tag)
        {
            auto shm_name = make_mailbox_name(owner_node, tag);
            regions_.emplace_back(SharedRegion::open(shm_name.c_str()));
            return Publisher(regions_.back());
        }

        std::string const& name()   const { return name_; }
        std::string const& prefix() const { return prefix_; }

    private:
        // Topic-centric: node name is NOT part of PubSub/Broadcast paths.
        // /{prefix}_{topic}
        std::string make_topic_name(char const* topic) const
        {
            return "/" + prefix_ + "_" + topic;
        }

        // /{prefix}_broadcast_{channel}
        std::string make_broadcast_name(char const* channel) const
        {
            return "/" + prefix_ + "_broadcast_" + channel;
        }

        // /{prefix}_{owner}_mbx_{tag}
        std::string make_mailbox_name(char const* owner, char const* tag) const
        {
            return "/" + prefix_ + "_" + owner + "_mbx_" + tag;
        }

        std::string name_;
        std::string prefix_;
        std::vector<SharedRegion> regions_;
    };

} // namespace kickmsg

#endif // KICKMSG_NODE_H
