#ifndef KICKCAT_SLAVE_EMULATED_NETWORK_H
#define KICKCAT_SLAVE_EMULATED_NETWORK_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "kickcat/ESC/EmulatedESC.h"
#include "kickcat/Frame.h"
#include "kickcat/OS/Time.h"

namespace kickcat
{
    // Physical model of an emulated EtherCAT segment: the slaves wired through
    // their ports plus the master injection point(s). It routes a frame through
    // the slaves in real physical order (EtherCAT port order 0 -> 3 -> 1 -> 2,
    // branches traversed depth-first, loopback at open or broken ports) instead of
    // blindly handing every datagram to every slave.
    //
    // The topology is static between control-plane events (build, or a wire
    // break/heal), so the processing order, DL_STATUS and DC receive-time offsets
    // are precomputed once and only recomputed when a link state changes. The
    // per-frame hot path is then a flat iteration over the precomputed order, with
    // no allocation - same cost as the historical "loop over all slaves".
    class EmulatedNetwork
    {
    public:
        static constexpr uint8_t PORT_COUNT = 4;
        static constexpr size_t  NO_NODE    = static_cast<size_t>(-1);

        // Slaves are given in nominal (head-to-tail) order and wired as a daisy
        // chain by default (slave[i].port1 <-> slave[i+1].port0, master injecting on
        // slave[0].port0) - the historical line behaviour. Call connect()/
        // setInjection()/setRedundancyInjection() to define a different topology.
        explicit EmulatedNetwork(std::vector<EmulatedESC*> slaves);

        // Topology definition. A connect() call drops the default line and lets the
        // caller wire the segment explicitly; the layout is (re)computed lazily on
        // the next route() / setLinkState().
        void connect(size_t node_a, uint8_t port_a, size_t node_b, uint8_t port_b);
        void setInjection(size_t node, uint8_t port);            // nominal master port (head)
        void setRedundancyInjection(size_t node, uint8_t port);  // tail master port; enables redundancy

        // Runtime fault injection: break or heal the wire between two slaves. Flips
        // both endpoints to loopback and triggers a one-time order/DL_STATUS rebuild.
        void setLinkState(size_t node_a, size_t node_b, bool up);

        // Route a received frame through the slaves in physical order. `redundancy`
        // selects the tail injection path (meaningful only when redundancy is set).
        void route(Frame& frame, bool redundancy = false);

        size_t size()          const { return slaves_.size(); }
        bool   hasRedundancy() const { return redundancy_node_ != NO_NODE; }

        // True when a frame injected at the head reaches the tail injection point, so
        // the ring is closed: a frame entering one master port leaves the other. When
        // a wire is broken each half loops back to the port it came from instead.
        bool   ringIntact()    const { return ring_intact_; }

    private:
        struct Port
        {
            size_t  peer_node = NO_NODE;   // connected slave, NO_NODE if open or to the master
            uint8_t peer_port = 0;
            bool    to_master = false;     // master injection point on this port
            bool    up        = true;      // physical link present (false == broken wire)
        };
        struct Node
        {
            EmulatedESC* esc;
            std::array<Port, PORT_COUNT> ports{};
        };

        void buildLine();
        void rebuild();
        void buildOrder(size_t node, uint8_t entry_port, std::vector<size_t>& order, std::vector<bool>& visited) const;
        nanoseconds buildReceiveTimes(size_t node, uint8_t entry_port, nanoseconds t_in, std::vector<bool>& visited);
        void computeDlStatus();
        void writeReceiveTimes(size_t node, nanoseconds base);

        std::vector<EmulatedESC*> slaves_;
        std::vector<Node>         nodes_;

        size_t  injection_node_  = 0;
        uint8_t injection_port_  = 0;
        size_t  redundancy_node_ = NO_NODE;
        uint8_t redundancy_port_ = 0;

        std::vector<size_t> order_nominal_;      // node indices in physical processing order
        std::vector<size_t> order_redundancy_;

        // Per-(node, port) DC receive-time offset and per-node EPU offset, relative
        // to frame entry; combined with a per-latch time base when a frame latches.
        std::vector<std::array<nanoseconds, PORT_COUNT>> recv_offset_;
        std::vector<nanoseconds> epu_offset_;

        bool custom_topology_ = false;   // true once connect() drops the default line
        bool ring_intact_ = false;       // head injection reaches the tail injection point
        bool dirty_ = true;
    };
}

#endif
