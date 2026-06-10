#include <gtest/gtest.h>

#include <memory>

#include "kickcat/EmulatedNetwork.h"
#include "kickcat/ESC/EmulatedESC.h"
#include "kickcat/Frame.h"

using namespace kickcat;

namespace
{
    std::vector<std::unique_ptr<EmulatedESC>> makeSlaves(size_t n)
    {
        std::vector<std::unique_ptr<EmulatedESC>> slaves;
        for (size_t i = 0; i < n; ++i)
        {
            slaves.push_back(std::make_unique<EmulatedESC>());
        }
        return slaves;
    }

    std::vector<EmulatedESC*> pointers(std::vector<std::unique_ptr<EmulatedESC>> const& slaves)
    {
        std::vector<EmulatedESC*> ptrs;
        for (auto const& s : slaves)
        {
            ptrs.push_back(s.get());
        }
        return ptrs;
    }

    // Route a single auto-increment write addressed to the k-th visited slave (the
    // exact mechanism the master uses in setAddresses). After routing, the slave at
    // processing position k holds `value`.
    void routeApwr(EmulatedNetwork& net, bool redundancy, uint16_t position, uint16_t offset,
                   uint16_t value)
    {
        Frame frame;
        frame.addDatagram(0, Command::APWR, createAddress(position, offset), &value, sizeof(value));
        frame.finalize();
        net.route(frame, redundancy);
    }

    // Assign a distinct station address to each slave in processing order and return
    // the discovered order as the list of slave indices (resolved from the values).
    std::vector<size_t> discoverOrder(EmulatedNetwork& net,
                                      std::vector<std::unique_ptr<EmulatedESC>> const& slaves,
                                      bool redundancy, size_t count)
    {
        for (size_t k = 0; k < count; ++k)
        {
            routeApwr(net, redundancy, static_cast<uint16_t>(0 - k), reg::STATION_ADDR,
                      static_cast<uint16_t>(0x1000 + k));
        }

        std::vector<size_t> order(count, EmulatedNetwork::NO_NODE);
        for (size_t i = 0; i < slaves.size(); ++i)
        {
            uint16_t addr = 0;
            slaves[i]->read(reg::STATION_ADDR, &addr, sizeof(addr));
            if (addr >= 0x1000 and addr < 0x1000 + count)
            {
                order[addr - 0x1000] = i;
            }
        }
        return order;
    }

    uint16_t dlStatus(EmulatedESC& esc)
    {
        uint16_t dl = 0;
        esc.read(reg::ESC_DL_STATUS, &dl, sizeof(dl));
        return dl;
    }

    bool pl(uint16_t dl, uint8_t port)   { return (dl >> (4 + port)) & 1u; }
    bool com(uint16_t dl, uint8_t port)  { return (dl >> (9 + 2 * port)) & 1u; }
    bool loop(uint16_t dl, uint8_t port) { return (dl >> (8 + 2 * port)) & 1u; }
}

TEST(EmulatedNetwork, line_processing_order_is_array_order)
{
    auto slaves = makeSlaves(4);
    EmulatedNetwork net(pointers(slaves));

    auto order = discoverOrder(net, slaves, false, 4);
    EXPECT_EQ(order, (std::vector<size_t>{0, 1, 2, 3}));
}

TEST(EmulatedNetwork, tree_processing_order_follows_0_3_1_2)
{
    auto slaves = makeSlaves(4);
    EmulatedNetwork net(pointers(slaves));

    // node0 is a junction: branch on port3 -> node2 -> node3, branch on port1 -> node1.
    net.connect(0, 3, 2, 0);
    net.connect(0, 1, 1, 0);
    net.connect(2, 1, 3, 0);
    net.setInjection(0, 0);

    // Port order 0->3->1->2: port3 branch (node2, then its child node3) before port1 (node1).
    auto order = discoverOrder(net, slaves, false, 4);
    EXPECT_EQ(order, (std::vector<size_t>{0, 2, 3, 1}));

    // node0 sees three open ports (master, node1, node2) -> the master reads it as a junction.
    uint16_t dl0 = dlStatus(*slaves[0]);
    EXPECT_TRUE(pl(dl0, 0) and pl(dl0, 1) and pl(dl0, 3));
    EXPECT_TRUE(com(dl0, 0) and com(dl0, 1) and com(dl0, 3));
    EXPECT_FALSE(pl(dl0, 2));
    EXPECT_TRUE(loop(dl0, 2));
    EXPECT_EQ(com(dl0, 0) + com(dl0, 1) + com(dl0, 2) + com(dl0, 3), 3);
}

TEST(EmulatedNetwork, dl_status_matches_legacy_line_topology)
{
    auto slaves = makeSlaves(3);
    EmulatedNetwork net(pointers(slaves));

    // Any frame triggers the one-time topology build that writes DL_STATUS.
    routeApwr(net, false, 0, reg::TYPE, 0);

    // Head/middle: ports 0 and 1 connected; tail: only port 0 connected (back-compat).
    for (size_t i = 0; i < 3; ++i)
    {
        uint16_t dl = dlStatus(*slaves[i]);
        EXPECT_TRUE(pl(dl, 0) and com(dl, 0));
        EXPECT_FALSE(pl(dl, 2) or pl(dl, 3));
        if (i + 1 < 3)
        {
            EXPECT_TRUE(pl(dl, 1) and com(dl, 1));
        }
        else
        {
            EXPECT_FALSE(pl(dl, 1) or com(dl, 1));
            EXPECT_TRUE(loop(dl, 1));
        }
    }
}

TEST(EmulatedNetwork, receive_time_latch_produces_ordered_deltas)
{
    auto slaves = makeSlaves(3);
    // Pin a known per-hop propagation delay so the expected latch deltas are exact.
    for (auto& s : slaves)
    {
        s->setForwardingDelay(100ns);
    }
    EmulatedNetwork net(pointers(slaves));

    uint8_t dummy = 0;
    Frame frame;
    frame.addDatagram(0, Command::BWR, createAddress(0, reg::DC_RECEIVED_TIME), &dummy, sizeof(dummy));
    frame.finalize();
    net.route(frame);

    uint32_t ports[3][4];
    uint64_t epu[3];
    for (size_t i = 0; i < 3; ++i)
    {
        slaves[i]->read(reg::DC_RECEIVED_TIME, ports[i], sizeof(ports[i]));
        slaves[i]->read(reg::DC_ECAT_RECEIVED_TIME, &epu[i], sizeof(epu[i]));
    }

    // The EtherCAT processing unit sees the frame later the deeper it sits.
    EXPECT_LT(epu[0], epu[1]);
    EXPECT_LT(epu[1], epu[2]);

    // Port1-minus-port0 is the round-trip through everything downstream: larger the
    // closer to the master, and strictly positive (non-degenerate) for inner slaves.
    uint32_t delta0 = ports[0][1] - ports[0][0];
    uint32_t delta1 = ports[1][1] - ports[1][0];
    EXPECT_GT(delta0, delta1);
    EXPECT_GT(delta1, 0u);
}

TEST(EmulatedNetwork, intact_ring_routes_every_slave_on_the_nominal_path_only)
{
    auto slaves = makeSlaves(3);
    EmulatedNetwork net(pointers(slaves));
    net.setRedundancyInjection(2, 1);   // close the ring on the tail slave

    // Nominal path covers all slaves...
    auto nominal = discoverOrder(net, slaves, false, 3);
    EXPECT_EQ(nominal, (std::vector<size_t>{0, 1, 2}));
    EXPECT_TRUE(net.ringIntact());

    // ...so the redundant path must cover none: a redundant write reaches no slave.
    // (If it did, each slave would be counted twice and the master's WKC sum - which
    // adds the nominal and redundant frames - would reject every datagram.)
    routeApwr(net, true, 0, reg::STATION_ADDR, 0xABCD);
    for (auto const& s : slaves)
    {
        uint16_t addr = 0;
        s->read(reg::STATION_ADDR, &addr, sizeof(addr));
        EXPECT_NE(addr, 0xABCD);
    }
}

TEST(EmulatedNetwork, set_link_state_ignores_out_of_range_nodes)
{
    auto slaves = makeSlaves(3);
    EmulatedNetwork net(pointers(slaves));

    // Out-of-range indices (e.g. from a bad --break-link CLI arg) must be a no-op,
    // not an out-of-bounds access. The topology stays the intact line.
    net.setLinkState(99, 100, false);
    auto order = discoverOrder(net, slaves, false, 3);
    EXPECT_EQ(order, (std::vector<size_t>{0, 1, 2}));
}

TEST(EmulatedNetwork, broken_wire_splits_into_two_redundancy_paths)
{
    auto slaves = makeSlaves(4);
    EmulatedNetwork net(pointers(slaves));
    net.setRedundancyInjection(3, 1);   // redundant master port on the tail slave

    // Break the wire between slave 1 and slave 2.
    net.setLinkState(1, 2, false);

    // Nominal frame from the head reaches only slaves 0 and 1, then loops back.
    auto nominal = discoverOrder(net, slaves, false, 2);
    EXPECT_EQ(nominal, (std::vector<size_t>{0, 1}));

    // Redundant frame from the tail reaches slaves 3 and 2.
    auto redundant = discoverOrder(net, slaves, true, 2);
    EXPECT_EQ(redundant, (std::vector<size_t>{3, 2}));

    // The two ports straddling the break now show loopback / no communication.
    uint16_t dl1 = dlStatus(*slaves[1]);
    uint16_t dl2 = dlStatus(*slaves[2]);
    EXPECT_FALSE(com(dl1, 1));
    EXPECT_TRUE(loop(dl1, 1));
    EXPECT_FALSE(com(dl2, 0));
    EXPECT_TRUE(loop(dl2, 0));

    // Healing restores the single intact line.
    net.setLinkState(1, 2, true);
    auto healed = discoverOrder(net, slaves, false, 4);
    EXPECT_EQ(healed, (std::vector<size_t>{0, 1, 2, 3}));
}
