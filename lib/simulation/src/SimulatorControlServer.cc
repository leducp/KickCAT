#include "kickcat/simulation/SimulatorControlServer.h"

namespace kickcat::sim
{
    SimulatorControlServer::SimulatorControlServer(EmulatedNetwork& network, size_t node_count)
        : network_(network)
        , node_count_(node_count)
    {
    }

    void SimulatorControlServer::attach(std::string const& name)
    {
        channel_.attach(name);
        attached_ = true;
    }

    void SimulatorControlServer::drain()
    {
        if (not attached_)
        {
            return;
        }
        // Stop if the event ring can't take an ack, so a command is never applied
        // without being acknowledged (host fell behind draining the return stream).
        ControlCommand cmd{};
        while (channel_.eventSpaceAvailable() and channel_.nextCommand(cmd))
        {
            channel_.sendEvent(apply(cmd));
        }
    }

    void SimulatorControlServer::publishStats(SimStats const& s)
    {
        if (not attached_)
        {
            return;
        }
        // Best-effort event: stats are lossy by nature, so a full ring just drops
        // this window. Acks keep their guaranteed slot via drain()'s space check.
        ControlEvent event{};
        event.type          = ControlEvent::Type::FrameStats;
        event.payload.stats = s;
        channel_.sendEvent(event);
    }

    ControlEvent SimulatorControlServer::apply(ControlCommand const& cmd)
    {
        ControlEvent response{};
        response.type                    = ControlEvent::Type::SetLinkAck;
        response.payload.set_link_ack.ok = 0;

        if (cmd.type == ControlCommand::Type::SetLink)
        {
            SetLink const& link = cmd.payload.set_link;
            bool const valid = (link.node_a < node_count_
                            and link.node_b < node_count_
                            and link.node_a != link.node_b);
            if (valid)
            {
                bool const up = (link.up != 0);
                network_.setLinkState(link.node_a, link.node_b, up);
                response.payload.set_link_ack.ok   = 1;
                response.payload.set_link_ack.link = link;
            }
        }

        return response;
    }
}
