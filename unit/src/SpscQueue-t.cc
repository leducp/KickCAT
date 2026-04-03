#include <gtest/gtest.h>
#include <cstring>

#include "mocks/Time.h"
#include "kickcat/SpscQueue.h"

using namespace kickcat;

class TestSpscQueue : public ::testing::Test
{
public:
    using Frame = uint8_t[1522];
    using Queue = SpscQueue<Frame, 64>;

    void SetUp() override
    {
        // Init free_top to empty
        slot_pool_.free_top = tagged_pack(0, INVALID_SLOT);

        // Push all slots into the free-stack
        for (uint32_t i = 0; i < POOL_SIZE; ++i)
        {
            treiber_push(slot_pool_.free_top, pool_, Queue::SLOT_STRIDE, i);
        }

        queue_a_.initContext();
        queue_b_.initContext();

        resetSinceEpoch();
    }

    static constexpr uint32_t POOL_SIZE = 128; // 64 per direction

    SlotPool slot_pool_{};
    alignas(CACHE_LINE) uint8_t pool_[POOL_SIZE * Queue::SLOT_STRIDE]{};

    struct SharedRegion
    {
        Queue::Context ctx_a;
        Queue::Context ctx_b;
    } shm_{};

    Queue queue_a_{shm_.ctx_a, slot_pool_, pool_};
    Queue queue_b_{shm_.ctx_b, slot_pool_, pool_};
};

TEST_F(TestSpscQueue, push_pop_nominal)
{
    uint32_t const PAYLOAD = 42;

    // Producer: allocate, write, publish
    auto item = queue_a_.allocate();
    ASSERT_NE(INVALID_SLOT, item.index);
    ASSERT_NE(nullptr, item.address);
    std::memcpy(item.address, &PAYLOAD, sizeof(PAYLOAD));
    item.len = sizeof(PAYLOAD);
    queue_a_.ready(item);

    // Consumer: get, read, free
    auto got = queue_a_.get(0ns);
    ASSERT_NE(INVALID_SLOT, got.index);
    ASSERT_EQ(sizeof(PAYLOAD), got.len);
    ASSERT_EQ(0, std::memcmp(got.address, &PAYLOAD, sizeof(PAYLOAD)));
    queue_a_.free(got);
}

TEST_F(TestSpscQueue, nothing_to_read)
{
    auto item = queue_a_.get(0ns);
    ASSERT_EQ(INVALID_SLOT, item.index);
    ASSERT_EQ(nullptr, item.address);
}

TEST_F(TestSpscQueue, multiple_frames)
{
    constexpr int COUNT = 10;
    for (int i = 0; i < COUNT; ++i)
    {
        auto item = queue_a_.allocate();
        ASSERT_NE(INVALID_SLOT, item.index);
        std::memcpy(item.address, &i, sizeof(i));
        item.len = sizeof(i);
        queue_a_.ready(item);
    }

    for (int i = 0; i < COUNT; ++i)
    {
        auto got = queue_a_.get(0ns);
        ASSERT_NE(INVALID_SLOT, got.index);
        int val = 0;
        std::memcpy(&val, got.address, sizeof(val));
        ASSERT_EQ(i, val);
        queue_a_.free(got);
    }

    // Queue empty now
    auto empty = queue_a_.get(0ns);
    ASSERT_EQ(INVALID_SLOT, empty.index);
}

TEST_F(TestSpscQueue, full_queue_evicts_oldest)
{
    // Fill all 64 slots of queue_a
    for (uint32_t i = 0; i < Queue::depth(); ++i)
    {
        auto item = queue_a_.allocate();
        ASSERT_NE(INVALID_SLOT, item.index) << "Failed to allocate slot " << i;
        item.len = 4;
        uint32_t val = i;
        std::memcpy(item.address, &val, sizeof(val));
        queue_a_.ready(item);
    }

    // Write one more — this should evict the oldest entry
    auto extra = queue_a_.allocate();
    ASSERT_NE(INVALID_SLOT, extra.index);
    uint32_t val = 999;
    std::memcpy(extra.address, &val, sizeof(val));
    extra.len = 4;
    queue_a_.ready(extra);
}

TEST_F(TestSpscQueue, bidirectional)
{
    uint32_t const A_VAL = 0xAA;
    uint32_t const B_VAL = 0xBB;

    // A -> B direction
    auto a_item = queue_a_.allocate();
    std::memcpy(a_item.address, &A_VAL, sizeof(A_VAL));
    a_item.len = sizeof(A_VAL);
    queue_a_.ready(a_item);

    // B -> A direction
    auto b_item = queue_b_.allocate();
    std::memcpy(b_item.address, &B_VAL, sizeof(B_VAL));
    b_item.len = sizeof(B_VAL);
    queue_b_.ready(b_item);

    // Read from each direction
    auto got_a = queue_a_.get(0ns);
    ASSERT_EQ(0, std::memcmp(got_a.address, &A_VAL, sizeof(A_VAL)));
    queue_a_.free(got_a);

    auto got_b = queue_b_.get(0ns);
    ASSERT_EQ(0, std::memcmp(got_b.address, &B_VAL, sizeof(B_VAL)));
    queue_b_.free(got_b);
}

TEST_F(TestSpscQueue, item_size_and_depth)
{
    ASSERT_EQ(1522, Queue::item_size());
    ASSERT_EQ(64u, Queue::depth());
}
