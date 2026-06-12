// End-to-end redundancy tests: a real Link over two sockets that physically route
// frames through an EmulatedNetwork ring. Covers the full path through Link::read()'s
// socket crossover down to mergeSplitLRW's segment attribution.
#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <memory>
#include <queue>
#include <vector>

#include "mocks/EmulatedNetworkHelpers.h"

#include "kickcat/AbstractSocket.h"
#include "kickcat/EmulatedNetwork.h"
#include "kickcat/Frame.h"
#include "kickcat/Link.h"

using namespace kickcat;

namespace
{
    constexpr int32_t  PI_SIZE    = 24;
    constexpr uint32_t INPUT_BASE = 0xA0B0C000;

    class RedundancyIntegration : public testing::Test
    {
    protected:
        void SetUp() override
        {
            slaves_ = makeSlaves(3);
            net_ = std::make_unique<EmulatedNetwork>(pointers(slaves_));
            net_->setRedundancyInjection(2, 1); // close the ring on the tail slave

            for (size_t i = 0; i < slaves_.size(); ++i)
            {
                configureOverlappedPdo(*slaves_[i], static_cast<uint32_t>(i * 8),
                                       static_cast<uint32_t>(INPUT_BASE + i));
            }

            socket_nominal_    = std::make_shared<PhysicalSocket>(*net_, NIC::NOMINAL);
            socket_redundancy_ = std::make_shared<PhysicalSocket>(*net_, NIC::REDUNDANCY);
            socket_nominal_->setPeer(socket_redundancy_.get());
            socket_redundancy_->setPeer(socket_nominal_.get());
            link_ = std::make_unique<Link>(socket_nominal_, socket_redundancy_, [](){});

            LogicalFrameDescription desc{};
            desc.address = 0;
            desc.logical_size = PI_SIZE;
            desc.pdo_size = PI_SIZE;
            desc.entries = {{3, 0, 4}, {3, 8, 4}, {3, 16, 4}};
            link_->setLogicalMapping({desc});
        }

        struct CycleResult
        {
            std::array<uint8_t, PI_SIZE> data{};
            uint16_t wkc{0};
            bool processed{false};
            int32_t errors{0};
            DatagramState error_state{DatagramState::OK};
        };

        CycleResult runLRWCycle(uint8_t pattern_base)
        {
            CycleResult result;
            uint8_t outputs[PI_SIZE];
            for (int32_t i = 0; i < PI_SIZE; ++i)
            {
                outputs[i] = static_cast<uint8_t>(pattern_base + i);
            }

            auto process = [&result](DatagramHeader const*, uint8_t const* data, uint16_t wkc)
            {
                result.processed = true;
                result.wkc = wkc;
                std::memcpy(result.data.data(), data, result.data.size());
                return DatagramState::OK;
            };
            auto error = [&result](DatagramState const& state)
            {
                ++result.errors;
                result.error_state = state;
            };

            link_->addDatagram(Command::LRW, 0, outputs, PI_SIZE, process, error);
            link_->processDatagrams();
            return result;
        }

        // Every byte of the dispatched payload: rebuilt input blocks at i*8, echoed
        // output bytes in the unmapped gaps.
        void checkMergedPayload(CycleResult const& result, uint8_t pattern_base, uint32_t input_base)
        {
            for (size_t i = 0; i < slaves_.size(); ++i)
            {
                uint32_t input = 0;
                std::memcpy(&input, result.data.data() + i * 8, sizeof(input));
                EXPECT_EQ(static_cast<uint32_t>(input_base + i), input);

                for (size_t b = 4; b < 8; ++b)
                {
                    EXPECT_EQ(static_cast<uint8_t>(pattern_base + i * 8 + b), result.data[i * 8 + b]);
                }
            }
        }

        void checkOutputsApplied(uint8_t pattern_base)
        {
            for (size_t i = 0; i < slaves_.size(); ++i)
            {
                uint8_t out[4];
                slaves_[i]->read(0x1200, out, sizeof(out));
                for (size_t b = 0; b < 4; ++b)
                {
                    EXPECT_EQ(static_cast<uint8_t>(pattern_base + i * 8 + b), out[b]);
                }
            }
        }

        void writeInputs(uint32_t base)
        {
            for (size_t i = 0; i < slaves_.size(); ++i)
            {
                uint32_t input = static_cast<uint32_t>(base + i);
                slaves_[i]->write(0x1100, &input, sizeof(input));
            }
        }

        std::vector<std::unique_ptr<EmulatedESC>> slaves_;
        std::unique_ptr<EmulatedNetwork> net_;
        std::shared_ptr<PhysicalSocket> socket_nominal_;
        std::shared_ptr<PhysicalSocket> socket_redundancy_;
        std::unique_ptr<Link> link_;
    };
}


TEST_F(RedundancyIntegration, intact_ring_lrw_full_wkc_and_inputs)
{
    auto result = runLRWCycle(0x80);

    EXPECT_TRUE(net_->ringIntact());
    EXPECT_TRUE(result.processed);
    EXPECT_EQ(0, result.errors);
    EXPECT_EQ(9, result.wkc);
    checkMergedPayload(result, 0x80, INPUT_BASE);
    checkOutputsApplied(0x80);
}

TEST_F(RedundancyIntegration, split_between_slave0_and_slave1_rebuilds_all_inputs)
{
    // Head copy reaches slave 0 only (wkc 3), tail copy slaves 1 and 2 (wkc 6); each
    // loops back to its own NIC, so Link::read() crosses them on dispatch.
    net_->setLinkState(0, 1, false);

    auto result = runLRWCycle(0x80);

    EXPECT_FALSE(net_->ringIntact());
    EXPECT_TRUE(result.processed);
    EXPECT_EQ(0, result.errors);
    EXPECT_EQ(9, result.wkc);
    checkMergedPayload(result, 0x80, INPUT_BASE);
    checkOutputsApplied(0x80);
}

TEST_F(RedundancyIntegration, split_between_slave1_and_slave2_rebuilds_all_inputs)
{
    // Prefix is now slaves 0 and 1 (head copy wkc 6), tail copy slave 2 only (wkc 3).
    net_->setLinkState(1, 2, false);

    auto result = runLRWCycle(0x80);

    EXPECT_FALSE(net_->ringIntact());
    EXPECT_TRUE(result.processed);
    EXPECT_EQ(0, result.errors);
    EXPECT_EQ(9, result.wkc);
    checkMergedPayload(result, 0x80, INPUT_BASE);
    checkOutputsApplied(0x80);
}

TEST_F(RedundancyIntegration, nominal_master_cable_cut_dispatches_tail_copy)
{
    // Dead head cable: the head frame is lost and the segment has no nominal injection
    // anymore, so slave 0's port 0 loops back and the tail walk covers all three slaves.
    socket_nominal_->cut_to_master = true;
    net_->setInjection(EmulatedNetwork::NO_NODE, 0);

    auto result = runLRWCycle(0x80);

    EXPECT_TRUE(result.processed);
    EXPECT_EQ(0, result.errors);
    EXPECT_EQ(9, result.wkc);
    checkMergedPayload(result, 0x80, INPUT_BASE);
    checkOutputsApplied(0x80);
}

TEST_F(RedundancyIntegration, redundancy_master_cable_cut_dispatches_head_copy)
{
    // Dead tail cable: the tail frame is lost and the ring is open at the tail, so the
    // head copy loops back at the last slave and covers all three on its own.
    socket_redundancy_->cut_to_master = true;
    net_->setRedundancyInjection(EmulatedNetwork::NO_NODE, 0);

    auto result = runLRWCycle(0x80);

    EXPECT_TRUE(result.processed);
    EXPECT_EQ(0, result.errors);
    EXPECT_EQ(9, result.wkc);
    checkMergedPayload(result, 0x80, INPUT_BASE);
    checkOutputsApplied(0x80);
}

TEST_F(RedundancyIntegration, copy_lost_in_flight_intact_ring_still_dispatches)
{
    // One copy of the pair dropped in flight while the ring is intact: the surviving copy
    // was fully processed and must carry the cycle alone, with no fallout on later cycles.
    socket_nominal_->drop_next_delivery = true; // loses the tail-injected copy

    auto result = runLRWCycle(0x80);
    EXPECT_TRUE(result.processed);
    EXPECT_EQ(0, result.errors);
    EXPECT_EQ(9, result.wkc);
    checkMergedPayload(result, 0x80, INPUT_BASE);

    writeInputs(0xD0E0F000);
    auto clean = runLRWCycle(0x40);
    EXPECT_TRUE(clean.processed);
    EXPECT_EQ(0, clean.errors);
    EXPECT_EQ(9, clean.wkc);
    checkMergedPayload(clean, 0x40, 0xD0E0F000);

    // Losing the head-injected copy is not coverable on an intact ring: the surviving
    // tail copy streamed through unprocessed, so the cycle degrades visibly (wkc 0).
    socket_redundancy_->drop_next_delivery = true;
    writeInputs(0xB0B0B000);
    auto degraded = runLRWCycle(0x20);
    EXPECT_TRUE(degraded.processed);
    EXPECT_EQ(0, degraded.wkc);

    writeInputs(0xC1C1C100);
    auto recovered = runLRWCycle(0x60);
    EXPECT_TRUE(recovered.processed);
    EXPECT_EQ(0, recovered.errors);
    EXPECT_EQ(9, recovered.wkc);
    checkMergedPayload(recovered, 0x60, 0xC1C1C100);
}

TEST_F(RedundancyIntegration, both_master_cables_cut_then_restored)
{
    socket_nominal_->cut_to_master = true;
    socket_redundancy_->cut_to_master = true;

    auto dead = runLRWCycle(0x80);
    EXPECT_FALSE(dead.processed);
    EXPECT_EQ(1, dead.errors);
    EXPECT_EQ(DatagramState::LOST, dead.error_state);

    socket_nominal_->cut_to_master = false;
    socket_redundancy_->cut_to_master = false;
    writeInputs(0xD0E0F000);

    auto recovered = runLRWCycle(0x40);
    EXPECT_TRUE(recovered.processed);
    EXPECT_EQ(0, recovered.errors);
    EXPECT_EQ(9, recovered.wkc);
    checkMergedPayload(recovered, 0x40, 0xD0E0F000);
    checkOutputsApplied(0x40);
}

TEST_F(RedundancyIntegration, heal_after_split_recovers_intact_behavior)
{
    net_->setLinkState(0, 1, false);
    auto degraded = runLRWCycle(0x80);
    EXPECT_TRUE(degraded.processed);
    EXPECT_EQ(9, degraded.wkc);
    checkMergedPayload(degraded, 0x80, INPUT_BASE);

    net_->setLinkState(0, 1, true);
    writeInputs(0xD0E0F000); // distinguishable from cycle 1 inputs

    auto healed = runLRWCycle(0x40);

    EXPECT_TRUE(net_->ringIntact());
    EXPECT_TRUE(healed.processed);
    EXPECT_EQ(0, healed.errors);
    EXPECT_EQ(9, healed.wkc);
    checkMergedPayload(healed, 0x40, 0xD0E0F000);
    checkOutputsApplied(0x40);
}

TEST_F(RedundancyIntegration, brd_split_or_merge_through_link)
{
    net_->setLinkState(0, 1, false);

    // Same value on both tail-segment slaves so the expected OR holds whether the
    // emulated BRD overwrites or ORs within one copy: the cross-copy OR is what's pinned.
    uint16_t const addr_head = 0x0022;
    uint16_t const addr_tail = 0x4400;
    slaves_[0]->write(reg::STATION_ADDR, &addr_head, sizeof(addr_head));
    slaves_[1]->write(reg::STATION_ADDR, &addr_tail, sizeof(addr_tail));
    slaves_[2]->write(reg::STATION_ADDR, &addr_tail, sizeof(addr_tail));

    uint16_t value = 0;
    uint16_t wkc = 0;
    bool processed = false;
    int32_t errors = 0;
    auto process = [&](DatagramHeader const*, uint8_t const* data, uint16_t wkc_received)
    {
        processed = true;
        wkc = wkc_received;
        std::memcpy(&value, data, sizeof(value));
        return DatagramState::OK;
    };
    auto error = [&errors](DatagramState const&)
    {
        ++errors;
    };

    link_->addDatagram(Command::BRD, createAddress(0, reg::STATION_ADDR), nullptr,
                       sizeof(uint16_t), process, error);
    link_->processDatagrams();

    EXPECT_TRUE(processed);
    EXPECT_EQ(0, errors);
    EXPECT_EQ(3, wkc);       // 1 (head segment) + 2 (tail segment)
    EXPECT_EQ(0x4422, value);
}
