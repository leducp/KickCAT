#include <gtest/gtest.h>
#include <memory>
#include <vector>

#include "kickcat/ESC/EmulatedESC.h"
#include "kickcat/EmulatedNetwork.h"
#include "kickcat/Frame.h"
#include "kickcat/OS/SharedMemory.h"
#include "kickcat/SimulatorControl.h"
#include "kickcat/simulation/SimulatorControlServer.h"

using namespace kickcat;
using namespace kickcat::sim;

namespace
{
    constexpr char const* SHM_NAME = "/kickcat_simctl_test";

    ControlCommand setLink(uint16_t a, uint16_t b, uint8_t up)
    {
        ControlCommand cmd{};
        cmd.type = ControlCommand::Type::SetLink;
        cmd.payload.set_link = {a, b, up};
        return cmd;
    }
}

class SimulatorControlTest : public ::testing::Test
{
protected:
    // A leftover segment from a crashed run would keep stale ring state.
    void SetUp() override    { SharedMemory::unlink(SHM_NAME); }
    void TearDown() override { SharedMemory::unlink(SHM_NAME); }
};

TEST_F(SimulatorControlTest, command_and_response_round_trip)
{
    ControlChannel host;
    host.create(SHM_NAME);
    ControlChannel sim;
    sim.attach(SHM_NAME);   // a second mapping of the same segment, as the fork would

    ControlCommand drained;
    EXPECT_FALSE(sim.nextCommand(drained));

    ASSERT_TRUE(host.sendCommand(setLink(2, 3, 0)));
    ControlCommand got;
    ASSERT_TRUE(sim.nextCommand(got));
    EXPECT_EQ(got.type, ControlCommand::Type::SetLink);
    EXPECT_EQ(got.payload.set_link.node_a, 2);
    EXPECT_EQ(got.payload.set_link.node_b, 3);
    EXPECT_EQ(got.payload.set_link.up, 0);
    EXPECT_FALSE(sim.nextCommand(got));

    ControlResponse outgoing{};
    outgoing.type = ControlCommand::Type::SetLink;
    outgoing.ok   = 1;
    outgoing.payload.set_link = {2, 3, 0};
    ASSERT_TRUE(sim.sendResponse(outgoing));
    ControlResponse response;
    ASSERT_TRUE(host.nextResponse(response));
    EXPECT_EQ(response.ok, 1);
    EXPECT_EQ(response.payload.set_link.node_a, 2);
    EXPECT_EQ(response.payload.set_link.node_b, 3);
    EXPECT_FALSE(host.nextResponse(response));
}

TEST_F(SimulatorControlTest, commands_preserve_order)
{
    ControlChannel host;
    host.create(SHM_NAME);
    ControlChannel sim;
    sim.attach(SHM_NAME);

    for (uint16_t i = 0; i < 5; ++i)
    {
        ASSERT_TRUE(host.sendCommand(setLink(i, static_cast<uint16_t>(i + 1), 0)));
    }
    for (uint16_t i = 0; i < 5; ++i)
    {
        ControlCommand c;
        ASSERT_TRUE(sim.nextCommand(c));
        EXPECT_EQ(c.payload.set_link.node_a, i);
        EXPECT_EQ(c.payload.set_link.node_b, static_cast<uint16_t>(i + 1));
    }
    ControlCommand c;
    EXPECT_FALSE(sim.nextCommand(c));
}

TEST_F(SimulatorControlTest, client_drives_server_against_network)
{
    std::vector<std::unique_ptr<EmulatedESC>> escs;
    std::vector<EmulatedESC*> esc_ptrs;
    for (int i = 0; i < 3; ++i)
    {
        escs.push_back(std::make_unique<EmulatedESC>());
        esc_ptrs.push_back(escs.back().get());
    }
    EmulatedNetwork network(std::move(esc_ptrs));

    SimulatorControlClient client;
    client.open(SHM_NAME);
    SimulatorControlServer server(network, network.size());
    server.attach(SHM_NAME);

    // Head-injected auto-increment write: position 0xFFFF targets the second-visited
    // slave (node 1). Returns whether it received the marker -- i.e. is still reachable.
    auto reachesNode1 = [&](uint16_t marker)
    {
        Frame frame;
        frame.addDatagram(0, Command::APWR, createAddress(0xFFFF, reg::STATION_ADDR), &marker, sizeof(marker));
        frame.finalize();
        network.route(frame, false);
        uint16_t got = 0;
        escs[1]->read(reg::STATION_ADDR, &got, sizeof(got));
        return got == marker;
    };

    EXPECT_TRUE(reachesNode1(0x1001));

    ASSERT_TRUE(client.breakLink(0, 1));
    server.drain();
    ControlResponse response;
    ASSERT_TRUE(client.nextResponse(response));
    EXPECT_EQ(response.ok, 1);
    EXPECT_EQ(response.payload.set_link.up, 0);
    EXPECT_FALSE(reachesNode1(0x2002));

    ASSERT_TRUE(client.healLink(0, 1));
    server.drain();
    ASSERT_TRUE(client.nextResponse(response));
    EXPECT_EQ(response.ok, 1);
    EXPECT_EQ(response.payload.set_link.up, 1);
    EXPECT_TRUE(reachesNode1(0x3003));

    // Out-of-range node and a self-link are rejected, leaving the network untouched.
    ASSERT_TRUE(client.breakLink(0, 9));
    ASSERT_TRUE(client.breakLink(2, 2));
    server.drain();
    ASSERT_TRUE(client.nextResponse(response));
    EXPECT_EQ(response.ok, 0);
    ASSERT_TRUE(client.nextResponse(response));
    EXPECT_EQ(response.ok, 0);
    EXPECT_TRUE(reachesNode1(0x4004));
}
