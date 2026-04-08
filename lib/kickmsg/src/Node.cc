#include "kickmsg/Node.h"

namespace kickmsg
{

    Node::Node(std::string const& name, std::string const& prefix)
        : name_{name}
        , prefix_{prefix}
    {
    }

    Publisher Node::advertise(char const* topic, ChannelConfig const& cfg)
    {
        auto shm_name = make_topic_name(topic);
        regions_.emplace_back(
            SharedRegion::create(shm_name.c_str(), channel::PubSub, cfg, name_.c_str()));
        return Publisher(regions_.back());
    }

    Subscriber Node::subscribe(char const* topic)
    {
        auto shm_name = make_topic_name(topic);
        regions_.emplace_back(SharedRegion::open(shm_name.c_str()));
        return Subscriber(regions_.back());
    }

    BroadcastHandle Node::join_broadcast(char const* channel, ChannelConfig const& cfg)
    {
        auto shm_name = make_broadcast_name(channel);
        regions_.emplace_back(
            SharedRegion::create_or_open(
                shm_name.c_str(), channel::Broadcast, cfg, name_.c_str()));
        auto& region = regions_.back();
        return BroadcastHandle{Publisher{region}, Subscriber{region}};
    }

    Subscriber Node::create_mailbox(char const* tag, ChannelConfig const& cfg)
    {
        ChannelConfig mbx_cfg = cfg;
        mbx_cfg.max_subscribers = 1;
        auto shm_name = make_mailbox_name(name_.c_str(), tag);
        regions_.emplace_back(
            SharedRegion::create(shm_name.c_str(), channel::PubSub, mbx_cfg, name_.c_str()));
        return Subscriber(regions_.back());
    }

    Publisher Node::open_mailbox(char const* owner_node, char const* tag)
    {
        auto shm_name = make_mailbox_name(owner_node, tag);
        regions_.emplace_back(SharedRegion::open(shm_name.c_str()));
        return Publisher(regions_.back());
    }

    std::string Node::make_topic_name(char const* topic) const
    {
        return "/" + prefix_ + "_" + topic;
    }

    std::string Node::make_broadcast_name(char const* channel) const
    {
        return "/" + prefix_ + "_broadcast_" + channel;
    }

    std::string Node::make_mailbox_name(char const* owner, char const* tag) const
    {
        return "/" + prefix_ + "_" + owner + "_mbx_" + tag;
    }
}
