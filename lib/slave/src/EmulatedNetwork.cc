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

    bool EmulatedNetwork::walkFrame(size_t node, uint8_t entry_port, std::vector<Hop>& order,
                                    size_t& exit_node, uint8_t& exit_port, size_t& budget) const
    {
        if (budget == 0)
        {
            return true; // malformed cyclic wiring: stop the walk instead of recursing forever
        }
        --budget;

        if (entry_port == 0)
        {
            order.push_back({node, false});
        }

        uint8_t start = indexInOrder(entry_port);
        for (uint8_t k = 1; k < PORT_COUNT; ++k)
        {
            uint8_t p = PORT_ORDER[(start + k) % PORT_COUNT];
            Port const& port = nodes_[node].ports[p];
            if (port.to_master)
            {
                exit_node = node;
                exit_port = p;
                return true; // frame leaves the segment through the master port
            }
            if (port.up and (port.peer_node != NO_NODE))
            {
                if (walkFrame(port.peer_node, port.peer_port, order, exit_node, exit_port, budget))
                {
                    return true;
                }
                if (p == 0)
                {
                    order.push_back({node, false}); // frame came back into port 0: EPU passage
                }
            }
            else if (p == 0)
            {
                order.push_back({node, true}); // closed port 0 loops back through the EPU
            }
        }
        return false; // frame leaves back through its entry port
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
        if (entry_port == 0)
        {
            epu_offset_[node] = t_in; // EPU sits right behind port 0 reception
        }

        nanoseconds const fwd = nodes_[node].esc->forwardingDelay();
        nanoseconds t = t_in + fwd;

        uint8_t start = indexInOrder(entry_port);
        for (uint8_t k = 1; k < PORT_COUNT; ++k)
        {
            uint8_t p = PORT_ORDER[(start + k) % PORT_COUNT];
            Port const& port = nodes_[node].ports[p];
            if (port.to_master)
            {
                break; // frame leaves the segment: nothing downstream sees it
            }
            if (port.up and (port.peer_node != NO_NODE) and (not visited[port.peer_node]))
            {
                t += CABLE_DELAY;
                nanoseconds t_child_out = buildReceiveTimes(port.peer_node, port.peer_port, t, visited);
                t = t_child_out + CABLE_DELAY;
                recv_offset_[node][p] = t;   // frame received back from the child branch
                if (p == 0)
                {
                    epu_offset_[node] = t;   // EPU passage on the way back through port 0
                }
                t += fwd;
            }
            else if ((p == 0) and ((not port.up) or (port.peer_node == NO_NODE)))
            {
                epu_offset_[node] = t;       // closed port 0: loopback through the EPU
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

    bool EmulatedNetwork::isValidInjection(size_t node, uint8_t port) const
    {
        // NO_NODE is size_t(-1): the node bound also rejects "no injection".
        return (node < nodes_.size()) and (port < PORT_COUNT);
    }

    void EmulatedNetwork::rebuild()
    {
        bool const has_nominal = isValidInjection(injection_node_, injection_port_);
        bool const has_redundancy = isValidInjection(redundancy_node_, redundancy_port_);

        // Master injection points are derived here so connect() only manages wires.
        for (auto& node : nodes_)
        {
            for (auto& port : node.ports)
            {
                port.to_master = false;
            }
        }
        if (has_nominal)
        {
            nodes_[injection_node_].ports[injection_port_].to_master = true;
        }
        if (has_redundancy)
        {
            nodes_[redundancy_node_].ports[redundancy_port_].to_master = true;
        }

        size_t const n = nodes_.size();
        // Worst case frame walk: both directions of every port of every node, plus the
        // injection hop - anything longer means a wiring cycle.
        size_t const walk_budget = 2 * PORT_COUNT * (n + 1);

        order_nominal_.clear();
        order_redundancy_.clear();
        ring_intact_ = false;

        size_t exit_node = NO_NODE;
        uint8_t exit_port = 0;
        if (has_nominal)
        {
            size_t budget = walk_budget;
            walkFrame(injection_node_, injection_port_, order_nominal_, exit_node, exit_port, budget);
        }

        if (has_redundancy)
        {
            // Ring closed <=> the head frame leaves the segment through the tail master port.
            ring_intact_ = (exit_node == redundancy_node_) and (exit_port == redundancy_port_);

            // With the ring closed the tail frame streams through every slave without an EPU
            // passage and exits at the head: the walk yields an empty order. On a break each
            // half is processed by exactly one frame, from the break outward - the partition
            // keeps the master's summed WKC correct.
            size_t budget = walk_budget;
            walkFrame(redundancy_node_, redundancy_port_, order_redundancy_, exit_node, exit_port, budget);
        }

        recv_offset_.assign(n, std::array<nanoseconds, PORT_COUNT>{});
        epu_offset_.assign(n, 0ns);
        std::vector<bool> visited_time(n, false);
        if (has_nominal)
        {
            buildReceiveTimes(injection_node_, injection_port_, 0ns, visited_time);
        }
        if (has_redundancy)
        {
            buildReceiveTimes(redundancy_node_, redundancy_port_, 0ns, visited_time);
        }

        computeDlStatus();
        dirty_ = false;
    }

    void EmulatedNetwork::writeReceiveTimes(size_t node, nanoseconds base)
    {
        // Receive times latch the slave's local clock: a drifting slave latches
        // drifted timestamps, which is what the master's offset computation consumes.
        EmulatedESC* esc = nodes_[node].esc;
        uint32_t ports_raw[PORT_COUNT];
        for (uint8_t p = 0; p < PORT_COUNT; ++p)
        {
            ports_raw[p] = static_cast<uint32_t>(esc->localClock(base + recv_offset_[node][p]).count());
        }
        esc->write(reg::DC_RECEIVED_TIME, ports_raw, sizeof(ports_raw));

        uint64_t epu = static_cast<uint64_t>(esc->localClock(base + epu_offset_[node]).count());
        esc->write(reg::DC_ECAT_RECEIVED_TIME, &epu, sizeof(epu));
    }

    bool EmulatedNetwork::route(Frame& frame, bool redundancy)
    {
        if (dirty_)
        {
            rebuild();
        }

        std::vector<Hop> const* order = &order_nominal_;
        if (redundancy)
        {
            order = &order_redundancy_;
        }

        frame.resetContext();
        bool destroyed = false;
        while (true)
        {
            auto [header, data, wkc] = frame.peekDatagram();
            if (header == nullptr)
            {
                break;
            }

            uint16_t offset = static_cast<uint16_t>(header->address >> 16);
            bool latch = isPhysicalWrite(header->command) and (offset == reg::DC_RECEIVED_TIME);

            for (auto const& hop : *order)
            {
                if (hop.port0_closed)
                {
                    if (header->circulating)
                    {
                        // Already circulated once: this ESC destroys the frame. Cut-through
                        // forwarding means upstream hops still process every datagram.
                        destroyed = true;
                        break;
                    }
                    header->circulating = 1;
                }
                nodes_[hop.node].esc->processDatagram(header, data, wkc);
            }

            if (destroyed)
            {
                continue;
            }

            if (latch)
            {
                nanoseconds base = since_ecat_epoch();
                for (auto const& hop : *order)
                {
                    writeReceiveTimes(hop.node, base);
                }
            }
        }
        return not destroyed;
    }
}
