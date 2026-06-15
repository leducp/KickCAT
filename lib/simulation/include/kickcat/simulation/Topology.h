#ifndef KICKCAT_SIMULATION_TOPOLOGY_H
#define KICKCAT_SIMULATION_TOPOLOGY_H

#include <optional>
#include <vector>

#include <nlohmann/json.hpp>

#include "kickcat/EmulatedNetwork.h"

namespace kickcat::sim
{
    // A validated topology. parseTopology() (pure) produces it, applyTopology()
    // consumes it -- split so the parse/validation is testable without a network.
    struct TopologyPlan
    {
        struct Endpoint { int node; int port; };
        struct Link     { int a; int port_a; int b; int port_b; };

        std::optional<Endpoint> injection;
        std::vector<Link>       links;
        std::optional<Endpoint> redundancy_injection;
    };

    // Parse + validate a --topology JSON. Every node/port index is checked
    // (EmulatedNetwork indexes nodes_/ports[] without checks). Throws on a bad index.
    TopologyPlan parseTopology(nlohmann::json const& topo, int node_count);

    // Apply a plan. Returns true if it set a redundancy injection (so the caller
    // skips its default ring).
    bool applyTopology(EmulatedNetwork& network, TopologyPlan const& plan);
}

#endif
