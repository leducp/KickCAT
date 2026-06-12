#include <gtest/gtest.h>

#include <memory>

#include "mocks/EmulatedNetworkHelpers.h"

#include "kickcat/EmulatedNetwork.h"
#include "kickcat/Frame.h"
#include "kickcat/Link.h"

using namespace kickcat;

namespace
{
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

TEST(EmulatedNetwork, split_ring_lrw_master_merge_rebuilds_inputs)
{
    auto slaves = makeSlaves(3);
    EmulatedNetwork net(pointers(slaves));
    net.setRedundancyInjection(2, 1);

    for (size_t i = 0; i < slaves.size(); ++i)
    {
        configureOverlappedPdo(*slaves[i], static_cast<uint32_t>(i * 8), static_cast<uint32_t>(0xA0B0C000 + i));
    }

    // Wire break between slave 0 and slave 1: the nominal copy reaches slave 0 only,
    // the redundancy copy reaches slaves 2 then 1.
    net.setLinkState(0, 1, false);

    uint8_t outputs[24];
    for (size_t i = 0; i < sizeof(outputs); ++i)
    {
        outputs[i] = static_cast<uint8_t>(0x80 + i);
    }

    Frame frame_head;
    frame_head.addDatagram(0, Command::LRW, 0, outputs, sizeof(outputs));
    frame_head.finalize();
    net.route(frame_head, false);

    Frame frame_tail;
    frame_tail.addDatagram(0, Command::LRW, 0, outputs, sizeof(outputs));
    frame_tail.finalize();
    net.route(frame_tail, true);

    frame_head.resetContext();
    auto [header_head, data_head, wkc_head] = frame_head.peekDatagram();
    frame_tail.resetContext();
    auto [header_tail, data_tail, wkc_tail] = frame_tail.peekDatagram();

    ASSERT_EQ(3, *wkc_head); // slave 0: read + write
    ASSERT_EQ(6, *wkc_tail); // slaves 1 and 2

    // Outputs reached the slaves whichever segment they ended on
    for (size_t i = 0; i < slaves.size(); ++i)
    {
        uint32_t out = 0;
        slaves[i]->read(0x1200, &out, sizeof(out));
        uint32_t expected = 0;
        std::memcpy(&expected, outputs + i * 8, sizeof(expected));
        EXPECT_EQ(expected, out);
    }

    // Master-side rebuild of the spliced response. On a split each copy loops back to the
    // interface it was injected on, and Link::read() crosses the sockets: the tail copy is
    // the merge's data_nominal argument, the head copy its data_redundancy argument.
    LogicalFrameDescription desc{};
    desc.logical_size = sizeof(outputs);
    desc.pdo_size = sizeof(outputs);
    desc.entries = {{3, 0, 4}, {3, 8, 4}, {3, 16, 4}};

    mergeSplitLRW(desc, data_tail, data_head, *wkc_tail, *wkc_head);

    for (size_t i = 0; i < slaves.size(); ++i)
    {
        uint32_t input = 0;
        std::memcpy(&input, data_tail + i * 8, sizeof(input));
        EXPECT_EQ(static_cast<uint32_t>(0xA0B0C000 + i), input);

        // The unmapped gap keeps the echoed output payload
        EXPECT_EQ(0, std::memcmp(data_tail + i * 8 + 4, outputs + i * 8 + 4, 4));
    }
}


TEST(EmulatedNetwork, intact_ring_sets_no_circulating_flag)
{
    auto slaves = makeSlaves(3);
    EmulatedNetwork net(pointers(slaves));
    net.setRedundancyInjection(2, 1);

    uint16_t dummy = 0;
    for (bool redundancy : {false, true})
    {
        Frame frame;
        frame.addDatagram(0, Command::BRD, createAddress(0, reg::TYPE), &dummy, sizeof(dummy));
        frame.finalize();
        EXPECT_TRUE(net.route(frame, redundancy));

        frame.resetContext();
        auto [header, data, wkc] = frame.peekDatagram();
        EXPECT_EQ(0, header->circulating);
    }
}


TEST(EmulatedNetwork, split_tail_copy_carries_circulating_flag)
{
    auto slaves = makeSlaves(3);
    EmulatedNetwork net(pointers(slaves));
    net.setRedundancyInjection(2, 1);
    net.setLinkState(0, 1, false);

    uint16_t dummy = 0;

    // Head copy loops back at slave 0's closed port 1: no closed-port-0 EPU passage.
    Frame frame_head;
    frame_head.addDatagram(0, Command::BRD, createAddress(0, reg::TYPE), &dummy, sizeof(dummy));
    frame_head.finalize();
    EXPECT_TRUE(net.route(frame_head, false));
    frame_head.resetContext();
    auto [header_head, data_head, wkc_head] = frame_head.peekDatagram();
    EXPECT_EQ(0, header_head->circulating);

    // Tail copy loops back at slave 1's closed port 0: that EPU passage sets the flag.
    Frame frame_tail;
    frame_tail.addDatagram(0, Command::BRD, createAddress(0, reg::TYPE), &dummy, sizeof(dummy));
    frame_tail.finalize();
    EXPECT_TRUE(net.route(frame_tail, true));
    frame_tail.resetContext();
    auto [header_tail, data_tail, wkc_tail] = frame_tail.peekDatagram();
    EXPECT_EQ(1, header_tail->circulating);
    EXPECT_EQ(2, *wkc_tail); // the flag does not prevent processing on this passage
}


TEST(EmulatedNetwork, already_circulating_frame_is_destroyed_at_closed_port0)
{
    auto slaves = makeSlaves(3);
    EmulatedNetwork net(pointers(slaves));
    net.setRedundancyInjection(2, 1);
    net.setLinkState(0, 1, false);

    uint16_t value = 0xBEEF;
    Frame frame;
    frame.addDatagram(0, Command::BWR, createAddress(0, reg::STATION_ADDR), &value, sizeof(value));
    frame.finalize();
    frame.resetContext();
    auto [header, data, wkc] = frame.peekDatagram();
    header->circulating = 1;

    EXPECT_FALSE(net.route(frame, true));

    // The destroying ESC sits at the break: no slave of the segment applied the write.
    for (auto const& s : slaves)
    {
        uint16_t addr = 0;
        s->read(reg::STATION_ADDR, &addr, sizeof(addr));
        EXPECT_NE(0xBEEF, addr);
    }
}


TEST(EmulatedNetwork, split_ring_epu_times_follow_redundancy_processing_order)
{
    auto slaves = makeSlaves(3);
    for (auto& s : slaves)
    {
        s->setForwardingDelay(100ns);
    }
    EmulatedNetwork net(pointers(slaves));
    net.setRedundancyInjection(2, 1);
    net.setLinkState(0, 1, false);

    // Latch on the tail path: enters slave 2 at port 1, streams to the break, loops back
    // at slave 1's closed port 0 and is processed from the break outward.
    uint8_t dummy = 0;
    Frame frame;
    frame.addDatagram(0, Command::BWR, createAddress(0, reg::DC_RECEIVED_TIME), &dummy, sizeof(dummy));
    frame.finalize();
    EXPECT_TRUE(net.route(frame, true));

    uint32_t ports[3][4];
    uint64_t epu[3];
    for (size_t i = 1; i < 3; ++i)
    {
        slaves[i]->read(reg::DC_RECEIVED_TIME, ports[i], sizeof(ports[i]));
        slaves[i]->read(reg::DC_ECAT_RECEIVED_TIME, &epu[i], sizeof(epu[i]));
    }

    // Port times follow the stream toward the break, EPU times the processing on the
    // way back: both slaves pass their EPU at the same offset (zero cable delay).
    EXPECT_EQ(100u, ports[1][1] - ports[2][1]); // slave 1 entered one hop after slave 2
    EXPECT_EQ(200u, ports[2][0] - ports[2][1]); // round trip to the break and back
    EXPECT_EQ(epu[1], epu[2]);
    EXPECT_EQ(200u, static_cast<uint32_t>(epu[1]) - ports[2][1]);
}


TEST(EmulatedNetwork, empty_network_routes_without_slaves)
{
    EmulatedNetwork net({});

    uint8_t dummy = 0;
    Frame frame;
    frame.addDatagram(0, Command::BRD, 0, &dummy, sizeof(dummy));
    frame.finalize();
    net.route(frame);

    frame.resetContext();
    auto [header, data, wkc] = frame.peekDatagram();
    EXPECT_EQ(0, *wkc);
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

TEST(EmulatedNetwork, injection_port_out_of_range_is_inert)
{
    auto slaves = makeSlaves(3);
    EmulatedNetwork net(pointers(slaves));
    net.setInjection(0, 7);

    uint8_t dummy = 0;
    Frame frame;
    frame.addDatagram(0, Command::BRD, createAddress(0, reg::TYPE), &dummy, sizeof(dummy));
    frame.finalize();
    net.route(frame);
    frame.resetContext();
    auto [header, data, wkc] = frame.peekDatagram();
    EXPECT_EQ(0, *wkc);

    // hasRedundancy() only validates the node; rebuild() also rejects the port. Pin
    // that split: the flag reads true but the redundancy path stays empty.
    net.setRedundancyInjection(2, 9);
    EXPECT_TRUE(net.hasRedundancy());

    Frame frame_red;
    frame_red.addDatagram(0, Command::BRD, createAddress(0, reg::TYPE), &dummy, sizeof(dummy));
    frame_red.finalize();
    net.route(frame_red, true);
    frame_red.resetContext();
    auto [header_red, data_red, wkc_red] = frame_red.peekDatagram();
    EXPECT_EQ(0, *wkc_red);
}

TEST(EmulatedNetwork, cyclic_wiring_terminates)
{
    auto slaves = makeSlaves(2);
    EmulatedNetwork net(pointers(slaves));

    // Two-node loop with the injection overlapping a wired port: without the walk
    // budget the frame walk would recurse forever between the two port-0 entries.
    net.connect(0, 1, 1, 0);
    net.connect(1, 1, 0, 0);
    net.setInjection(0, 0);

    uint8_t dummy = 0;
    Frame frame;
    frame.addDatagram(0, Command::BRD, createAddress(0, reg::TYPE), &dummy, sizeof(dummy));
    frame.finalize();
    net.route(frame);   // returning at all is the assertion

    frame.resetContext();
    auto [header, data, wkc] = frame.peekDatagram();
    // The property under test is termination with a bounded number of EPU passes, not
    // the exact budget value.
    EXPECT_GT(*wkc, 0);
    EXPECT_LE(*wkc, 100);
}

TEST(EmulatedNetwork, circulating_destroy_processes_remaining_datagrams_upstream)
{
    auto slaves = makeSlaves(3);
    EmulatedNetwork net(pointers(slaves));

    // Mis-wired star: both stubs are entered through port 1, so their closed port 0
    // loops the frame back. The first stub sets the circulating flag, the second
    // destroys the frame.
    net.connect(0, 1, 1, 1);
    net.connect(0, 2, 2, 1);
    net.setInjection(0, 0);

    uint16_t value_a = 0x1111;
    uint16_t value_b = 0x2222;
    Frame frame;
    frame.addDatagram(0, Command::BWR, createAddress(0, reg::STATION_ADDR), &value_a, sizeof(value_a));
    frame.addDatagram(1, Command::BWR, createAddress(0, reg::STATION_ADDR), &value_b, sizeof(value_b));
    frame.finalize();

    EXPECT_FALSE(net.route(frame));

    // Cut-through forwarding: every hop upstream of the destroyer processed BOTH
    // datagrams before the frame died (the second write wins); the destroyer saw none.
    for (size_t i = 0; i < 2; ++i)
    {
        uint16_t addr = 0;
        slaves[i]->read(reg::STATION_ADDR, &addr, sizeof(addr));
        EXPECT_EQ(value_b, addr);
    }
    uint16_t untouched = 0;
    slaves[2]->read(reg::STATION_ADDR, &untouched, sizeof(untouched));
    EXPECT_EQ(0, untouched);
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

    // The redundant frame streams unprocessed to the break, loops back through the
    // EPU at the closed port 0, and is processed from the break outward: 2 then 3
    // (real ESCs process on the port0 path only, not at port of entry).
    auto redundant = discoverOrder(net, slaves, true, 2);
    EXPECT_EQ(redundant, (std::vector<size_t>{2, 3}));

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
