#include "kickcat/EmulatedNetwork.h"

namespace kickcat
{
    namespace
    {
        // EtherCAT port forwarding/processing order.
        constexpr std::array<uint8_t, 4> PORT_ORDER{0, 3, 1, 2};

        uint8_t indexInOrder(uint8_t port)
        {
            for (uint8_t i = 0; i < 4; ++i)
            {
                if (PORT_ORDER[i] == port)
                {
                    return i;
                }
            }
            return 0;
        }

        bool isPhysicalWrite(Command command)
        {
            switch (command)
            {
                case Command::BWR:
                case Command::BRW:
                case Command::APWR:
                case Command::APRW:
                case Command::FPWR:
                case Command::FPRW:
                {
                    return true;
                }
                default:
                {
                    return false;
                }
            }
        }

        // Cable propagation delay between two ESCs. Kept at zero for now: the per-ESC
        // forwarding delay alone yields ordered, non-degenerate port deltas.
        constexpr nanoseconds CABLE_DELAY = 0ns;
    }

    EmulatedNetwork::EmulatedNetwork(std::vector<EmulatedESC*> slaves)
        : slaves_(std::move(slaves))
    {
        nodes_.resize(slaves_.size());
        for (size_t i = 0; i < slaves_.size(); ++i)
        {
            nodes_[i].esc = slaves_[i];
        }
        buildLine();
    }

    void EmulatedNetwork::buildLine()
    {
        for (auto& node : nodes_)
        {
            node.ports = {};
        }
        for (size_t i = 0; i + 1 < nodes_.size(); ++i)
        {
            nodes_[i].ports[1]     = Port{i + 1, 0, false, true};
            nodes_[i + 1].ports[0] = Port{i,     1, false, true};
        }
        injection_node_  = 0;
        injection_port_  = 0;
        redundancy_node_ = NO_NODE;
        custom_topology_ = false;
        dirty_           = true;
    }

    void EmulatedNetwork::connect(size_t node_a, uint8_t port_a, size_t node_b, uint8_t port_b)
    {
        if (not custom_topology_)
        {
            // First explicit link drops the default daisy chain.
            for (auto& node : nodes_)
            {
                node.ports = {};
            }
            custom_topology_ = true;
        }
        nodes_[node_a].ports[port_a] = Port{node_b, port_b, false, true};
        nodes_[node_b].ports[port_b] = Port{node_a, port_a, false, true};
        dirty_ = true;
    }

    void EmulatedNetwork::setInjection(size_t node, uint8_t port)
    {
        injection_node_ = node;
        injection_port_ = port;
        dirty_ = true;
    }

    void EmulatedNetwork::setRedundancyInjection(size_t node, uint8_t port)
    {
        redundancy_node_ = node;
        redundancy_port_ = port;
        dirty_ = true;
    }

    void EmulatedNetwork::setLinkState(size_t node_a, size_t node_b, bool up)
    {
        if ((node_a >= nodes_.size()) or (node_b >= nodes_.size()))
        {
            return;  // no such link: ignore rather than index out of bounds
        }
        for (uint8_t p = 0; p < PORT_COUNT; ++p)
        {
            Port& port = nodes_[node_a].ports[p];
            if (port.peer_node == node_b)
            {
                port.up = up;
                nodes_[node_b].ports[port.peer_port].up = up;
            }
        }
        rebuild();
    }

    void EmulatedNetwork::buildOrder(size_t node, uint8_t entry_port, std::vector<size_t>& order,
                                     std::vector<bool>& visited) const
    {
        if (visited[node])
        {
            return;
        }
        visited[node] = true;
        order.push_back(node);

        uint8_t start = indexInOrder(entry_port);
        for (uint8_t k = 1; k < PORT_COUNT; ++k)
        {
            uint8_t p = PORT_ORDER[(start + k) % PORT_COUNT];
            Port const& port = nodes_[node].ports[p];
            if (port.up and (port.peer_node != NO_NODE) and (not visited[port.peer_node]))
            {
                buildOrder(port.peer_node, port.peer_port, order, visited);
            }
        }
    }

    nanoseconds EmulatedNetwork::buildReceiveTimes(size_t node, uint8_t entry_port, nanoseconds t_in,
                                                   std::vector<bool>& visited)
    {
        if (visited[node])
        {
            return t_in;
        }
        visited[node] = true;

        recv_offset_[node][entry_port] = t_in;
        epu_offset_[node]              = t_in;

        nanoseconds const fwd = nodes_[node].esc->forwardingDelay();
        nanoseconds t = t_in + fwd;

        uint8_t start = indexInOrder(entry_port);
        for (uint8_t k = 1; k < PORT_COUNT; ++k)
        {
            uint8_t p = PORT_ORDER[(start + k) % PORT_COUNT];
            Port const& port = nodes_[node].ports[p];
            if (port.up and (port.peer_node != NO_NODE) and (not visited[port.peer_node]))
            {
                t += CABLE_DELAY;
                nanoseconds t_child_out = buildReceiveTimes(port.peer_node, port.peer_port, t, visited);
                t = t_child_out + CABLE_DELAY;
                recv_offset_[node][p] = t;   // frame received back from the child branch
                t += fwd;
            }
        }
        return t;
    }

    void EmulatedNetwork::computeDlStatus()
    {
        for (auto& node : nodes_)
        {
            uint16_t dl = 0;
            for (uint8_t p = 0; p < PORT_COUNT; ++p)
            {
                Port const& port = node.ports[p];
                bool linked = port.to_master or ((port.peer_node != NO_NODE) and port.up);
                if (linked)
                {
                    dl |= static_cast<uint16_t>(1u << (4 + p));        // PL_portX
                    dl |= static_cast<uint16_t>(1u << (9 + 2 * p));    // COM_portX
                }
                else
                {
                    dl |= static_cast<uint16_t>(1u << (8 + 2 * p));    // LOOP_portX
                }
            }
            node.esc->write(reg::ESC_DL_STATUS, &dl, sizeof(dl));
        }
    }

    void EmulatedNetwork::rebuild()
    {
        // Master injection points are derived here so connect() only manages wires.
        for (auto& node : nodes_)
        {
            for (auto& port : node.ports)
            {
                port.to_master = false;
            }
        }
        if (injection_node_ != NO_NODE)
        {
            nodes_[injection_node_].ports[injection_port_].to_master = true;
        }
        if (redundancy_node_ != NO_NODE)
        {
            nodes_[redundancy_node_].ports[redundancy_port_].to_master = true;
        }

        size_t const n = nodes_.size();

        order_nominal_.clear();
        std::vector<bool> visited_order(n, false);
        buildOrder(injection_node_, injection_port_, order_nominal_, visited_order);

        order_redundancy_.clear();
        ring_intact_ = false;
        if (redundancy_node_ != NO_NODE)
        {
            // Ring is closed when the head injection already reaches the tail port.
            ring_intact_ = visited_order[redundancy_node_];
            // The two paths PARTITION the slaves (shared visited set): the redundant
            // path covers only what the nominal path could not reach. So each slave is
            // processed by exactly one frame and the master's WKC sum stays correct -
            // all slaves on the nominal frame when intact, head/tail split on a break.
            buildOrder(redundancy_node_, redundancy_port_, order_redundancy_, visited_order);
        }

        recv_offset_.assign(n, std::array<nanoseconds, PORT_COUNT>{});
        epu_offset_.assign(n, 0ns);
        std::vector<bool> visited_time(n, false);
        buildReceiveTimes(injection_node_, injection_port_, 0ns, visited_time);
        if (redundancy_node_ != NO_NODE)
        {
            buildReceiveTimes(redundancy_node_, redundancy_port_, 0ns, visited_time);
        }

        computeDlStatus();
        dirty_ = false;
    }

    void EmulatedNetwork::writeReceiveTimes(size_t node, nanoseconds base)
    {
        uint32_t ports_raw[PORT_COUNT];
        for (uint8_t p = 0; p < PORT_COUNT; ++p)
        {
            ports_raw[p] = static_cast<uint32_t>((base + recv_offset_[node][p]).count());
        }
        nodes_[node].esc->write(reg::DC_RECEIVED_TIME, ports_raw, sizeof(ports_raw));

        uint64_t epu = static_cast<uint64_t>((base + epu_offset_[node]).count());
        nodes_[node].esc->write(reg::DC_ECAT_RECEIVED_TIME, &epu, sizeof(epu));
    }

    void EmulatedNetwork::route(Frame& frame, bool redundancy)
    {
        if (dirty_)
        {
            rebuild();
        }

        std::vector<size_t> const* order = &order_nominal_;
        if (redundancy)
        {
            order = &order_redundancy_;
        }

        frame.resetContext();
        while (true)
        {
            auto [header, data, wkc] = frame.peekDatagram();
            if (header == nullptr)
            {
                break;
            }

            uint16_t offset = static_cast<uint16_t>(header->address >> 16);
            bool latch = isPhysicalWrite(header->command) and (offset == reg::DC_RECEIVED_TIME);

            for (size_t idx : *order)
            {
                nodes_[idx].esc->processDatagram(header, data, wkc);
            }

            if (latch)
            {
                nanoseconds base = since_ecat_epoch();
                for (size_t idx : *order)
                {
                    writeReceiveTimes(idx, base);
                }
            }
        }
    }
}
