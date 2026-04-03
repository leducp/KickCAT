#include <gtest/gtest.h>
#include <cstring>
#include <vector>
#include <algorithm>
#include <numeric>

#include "kickcat/Treiber.h"

using namespace kickcat;

class TestTreiber : public ::testing::Test
{
public:
    static constexpr uint32_t POOL_SIZE   = 16;
    static constexpr std::size_t DATA_SIZE  = 64;
    static constexpr std::size_t SLOT_STRIDE = align_up(sizeof(SlotMeta) + DATA_SIZE, CACHE_LINE);

    void SetUp() override
    {
        // Zero-init pool
        std::memset(pool_, 0, sizeof(pool_));

        // Init free_top to empty
        free_top_.store(tagged_pack(0, INVALID_SLOT), std::memory_order_relaxed);
    }

    alignas(CACHE_LINE) uint8_t pool_[POOL_SIZE * SLOT_STRIDE]{};
    alignas(CACHE_LINE) std::atomic<uint64_t> free_top_{};
};

TEST_F(TestTreiber, pop_empty_returns_invalid)
{
    ASSERT_EQ(INVALID_SLOT, treiber_pop(free_top_, pool_, SLOT_STRIDE));
}

TEST_F(TestTreiber, push_then_pop_returns_same_slot)
{
    treiber_push(free_top_, pool_, SLOT_STRIDE, 5);

    uint32_t idx = treiber_pop(free_top_, pool_, SLOT_STRIDE);
    ASSERT_EQ(5u, idx);

    // Now empty again
    ASSERT_EQ(INVALID_SLOT, treiber_pop(free_top_, pool_, SLOT_STRIDE));
}

TEST_F(TestTreiber, push_all_pop_all)
{
    // Push all slots
    for (uint32_t i = 0; i < POOL_SIZE; ++i)
    {
        treiber_push(free_top_, pool_, SLOT_STRIDE, i);
    }

    // Pop all slots — should get all indices back (LIFO order)
    std::vector<uint32_t> popped;
    for (uint32_t i = 0; i < POOL_SIZE; ++i)
    {
        uint32_t idx = treiber_pop(free_top_, pool_, SLOT_STRIDE);
        ASSERT_NE(INVALID_SLOT, idx);
        popped.push_back(idx);
    }

    // Verify we got all unique indices 0..POOL_SIZE-1
    std::sort(popped.begin(), popped.end());
    std::vector<uint32_t> expected(POOL_SIZE);
    std::iota(expected.begin(), expected.end(), 0);
    ASSERT_EQ(expected, popped);

    // Stack is empty
    ASSERT_EQ(INVALID_SLOT, treiber_pop(free_top_, pool_, SLOT_STRIDE));
}

TEST_F(TestTreiber, generation_counter_increments)
{
    uint64_t initial = free_top_.load(std::memory_order_relaxed);
    ASSERT_EQ(0u, tagged_gen(initial));

    treiber_push(free_top_, pool_, SLOT_STRIDE, 0);
    uint64_t after_push = free_top_.load(std::memory_order_relaxed);
    ASSERT_EQ(1u, tagged_gen(after_push));

    treiber_pop(free_top_, pool_, SLOT_STRIDE);
    uint64_t after_pop = free_top_.load(std::memory_order_relaxed);
    ASSERT_EQ(2u, tagged_gen(after_pop));
}

TEST_F(TestTreiber, slot_data_access)
{
    treiber_push(free_top_, pool_, SLOT_STRIDE, 3);

    uint32_t idx = treiber_pop(free_top_, pool_, SLOT_STRIDE);
    ASSERT_EQ(3u, idx);

    SlotMeta* meta = slot_meta_at(pool_, SLOT_STRIDE, idx);
    uint8_t* data = slot_data(meta);

    // Write and read back data
    std::memset(data, 0xAB, DATA_SIZE);
    ASSERT_EQ(0xAB, data[0]);
    ASSERT_EQ(0xAB, data[DATA_SIZE - 1]);

    // Return slot
    treiber_push(free_top_, pool_, SLOT_STRIDE, idx);
}

TEST_F(TestTreiber, interleaved_push_pop)
{
    // Push 0, 1, 2
    treiber_push(free_top_, pool_, SLOT_STRIDE, 0);
    treiber_push(free_top_, pool_, SLOT_STRIDE, 1);
    treiber_push(free_top_, pool_, SLOT_STRIDE, 2);

    // Pop one (should be 2, LIFO)
    uint32_t idx = treiber_pop(free_top_, pool_, SLOT_STRIDE);
    ASSERT_EQ(2u, idx);

    // Push 5
    treiber_push(free_top_, pool_, SLOT_STRIDE, 5);

    // Pop remaining: 5, 1, 0
    idx = treiber_pop(free_top_, pool_, SLOT_STRIDE);
    ASSERT_EQ(5u, idx);

    idx = treiber_pop(free_top_, pool_, SLOT_STRIDE);
    ASSERT_EQ(1u, idx);

    idx = treiber_pop(free_top_, pool_, SLOT_STRIDE);
    ASSERT_EQ(0u, idx);

    ASSERT_EQ(INVALID_SLOT, treiber_pop(free_top_, pool_, SLOT_STRIDE));
}

TEST_F(TestTreiber, tagged_pack_roundtrip)
{
    uint32_t gen = 0xDEAD;
    uint32_t idx = 0xBEEF;
    uint64_t tagged = tagged_pack(gen, idx);

    ASSERT_EQ(gen, tagged_gen(tagged));
    ASSERT_EQ(idx, tagged_idx(tagged));
}

TEST_F(TestTreiber, tagged_pack_with_invalid_slot)
{
    uint64_t tagged = tagged_pack(42, INVALID_SLOT);
    ASSERT_EQ(42u, tagged_gen(tagged));
    ASSERT_EQ(INVALID_SLOT, tagged_idx(tagged));
}
