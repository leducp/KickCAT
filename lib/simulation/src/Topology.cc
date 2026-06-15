#include "kickcat/simulation/Topology.h"

#include <stdexcept>
#include <string>

namespace kickcat::sim
{
    using json = nlohmann::json;

    namespace
    {
        TopologyPlan::Endpoint parseEndpoint(json const& obj, int default_node, int default_port,
                                             int node_count, char const* what)
        {
            TopologyPlan::Endpoint ep{obj.value("node", default_node), obj.value("port", default_port)};
            bool node_ok = (ep.node >= 0) and (ep.node < node_count);
            bool port_ok = (ep.port >= 0) and (ep.port < EmulatedNetwork::PORT_COUNT);
            if (not node_ok or not port_ok)
            {
                throw std::runtime_error(std::string("topology ") + what + " out of range (node "
                    + std::to_string(ep.node) + ", port " + std::to_string(ep.port) + ")");
            }
            return ep;
        }
    }

    TopologyPlan parseTopology(json const& topo, int node_count)
    {
        TopologyPlan plan;

        if (topo.contains("injection"))
        {
            plan.injection = parseEndpoint(topo["injection"], 0, 0, node_count, "injection");
        }

        for (const auto& link : topo.value("links", json::array()))
        {
            TopologyPlan::Link l{link.value("a", -1), link.value("port_a", -1),
                                 link.value("b", -1), link.value("port_b", -1)};
            bool nodes_ok = (l.a >= 0) and (l.a < node_count) and (l.b >= 0) and (l.b < node_count);
            bool ports_ok = (l.port_a >= 0) and (l.port_a < EmulatedNetwork::PORT_COUNT)
                        and (l.port_b >= 0) and (l.port_b < EmulatedNetwork::PORT_COUNT);
            if (not nodes_ok or not ports_ok)
            {
                throw std::runtime_error("topology link out of range: {a:" + std::to_string(l.a)
                    + ", port_a:" + std::to_string(l.port_a) + ", b:" + std::to_string(l.b)
                    + ", port_b:" + std::to_string(l.port_b) + "}");
            }
            plan.links.push_back(l);
        }

        if (topo.contains("redundancy_injection"))
        {
            plan.redundancy_injection =
                parseEndpoint(topo["redundancy_injection"], node_count - 1, 1, node_count, "redundancy_injection");
        }

        return plan;
    }

    bool applyTopology(EmulatedNetwork& network, TopologyPlan const& plan)
    {
        if (plan.injection)
        {
            network.setInjection(static_cast<size_t>(plan.injection->node),
                                 static_cast<uint8_t>(plan.injection->port));
        }
        for (auto const& l : plan.links)
        {
            network.connect(static_cast<size_t>(l.a), static_cast<uint8_t>(l.port_a),
                            static_cast<size_t>(l.b), static_cast<uint8_t>(l.port_b));
        }
        if (plan.redundancy_injection)
        {
            network.setRedundancyInjection(static_cast<size_t>(plan.redundancy_injection->node),
                                           static_cast<uint8_t>(plan.redundancy_injection->port));
            return true;
        }
        return false;
    }
}
