#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "kickcat/simulation/Topology.h"

using namespace kickcat;
using namespace kickcat::sim;
using json = nlohmann::json;

TEST(ParseTopology, empty_object_is_empty_plan)
{
    TopologyPlan plan = parseTopology(json::object(), 4);
    EXPECT_FALSE(plan.injection.has_value());
    EXPECT_TRUE(plan.links.empty());
    EXPECT_FALSE(plan.redundancy_injection.has_value());
}

TEST(ParseTopology, parses_injection_links_redundancy)
{
    json topo = {
        {"injection", {{"node", 0}, {"port", 0}}},
        {"links", json::array({
            {{"a", 0}, {"port_a", 1}, {"b", 1}, {"port_b", 0}},
            {{"a", 0}, {"port_a", 3}, {"b", 2}, {"port_b", 0}},
        })},
        {"redundancy_injection", {{"node", 2}, {"port", 1}}},
    };
    TopologyPlan plan = parseTopology(topo, 3);

    ASSERT_TRUE(plan.injection.has_value());
    EXPECT_EQ(plan.injection->node, 0);
    ASSERT_EQ(plan.links.size(), 2u);
    EXPECT_EQ(plan.links[1].a, 0);
    EXPECT_EQ(plan.links[1].port_a, 3);
    EXPECT_EQ(plan.links[1].b, 2);
    ASSERT_TRUE(plan.redundancy_injection.has_value());
    EXPECT_EQ(plan.redundancy_injection->node, 2);
}

TEST(ParseTopology, rejects_out_of_range_node)
{
    json topo = {{"links", json::array({{{"a", 0}, {"port_a", 1}, {"b", 5}, {"port_b", 0}}})}};
    EXPECT_THROW(parseTopology(topo, 3), std::runtime_error);  // node 5 >= count 3
}

TEST(ParseTopology, rejects_out_of_range_port)
{
    json topo = {{"injection", {{"node", 0}, {"port", 9}}}};
    EXPECT_THROW(parseTopology(topo, 3), std::runtime_error);  // port 9 >= PORT_COUNT
}
