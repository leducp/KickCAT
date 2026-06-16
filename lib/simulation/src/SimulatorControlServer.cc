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
        // Stop if the response ring can't take an ack, so a command is never
        // applied without being acknowledged (host fell behind draining responses).
        ControlCommand cmd{};
        while (channel_.responseSpaceAvailable() and channel_.nextCommand(cmd))
        {
            channel_.sendResponse(apply(cmd));
        }
    }

    ControlResponse SimulatorControlServer::apply(ControlCommand const& cmd)
    {
        ControlResponse response{};
        response.type = cmd.type;
        response.ok   = 0;

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
                response.ok               = 1;
                response.payload.set_link = link;
            }
        }

        return response;
    }
}
