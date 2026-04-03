#include <gtest/gtest.h>
#include <cstring>
#include <thread>
#include <atomic>
#include <set>

#include "kickcat/SpscQueue.h"

using namespace kickcat;

// Stress tests use a small queue (depth=16) with a small frame
// to maximize wraparound and Treiber contention.
// All get() calls use 0ns timeout (non-blocking) with yield loops
// to avoid dependency on mocked since_epoch().

class StressSpscQueue : public ::testing::Test
{
public:
    using SmallFrame = uint8_t[64];
    using SmallQueue = SpscQueue<SmallFrame, 16>;

    // Pool size = ring depth. Each direction must have its own pool
    // to guarantee the ring never wraps past unconsumed entries.
    static constexpr uint32_t POOL_SIZE = SmallQueue::depth();

    void SetUp() override
    {
        slot_pool_.free_top = tagged_pack(0, INVALID_SLOT);
        for (uint32_t i = 0; i < POOL_SIZE; ++i)
        {
            treiber_push(slot_pool_.free_top, pool_, SmallQueue::SLOT_STRIDE, i);
        }
        queue_.initContext();
    }

    SlotPool slot_pool_{};
    alignas(CACHE_LINE) uint8_t pool_[POOL_SIZE * SmallQueue::SLOT_STRIDE]{};
    SmallQueue::Context ctx_{};
    SmallQueue queue_{ctx_, slot_pool_, pool_};
};

TEST_F(StressSpscQueue, concurrent_producer_consumer)
{
    // Producer sends N frames with sequential counters.
    // Consumer receives them, verifies strict ordering, and frees slots.
    // Stresses: Treiber free-stack contention, ring wraparound, sequence barriers.
    constexpr uint32_t N = 100'000;
    std::atomic<bool> error{false};

    std::thread producer([&]()
    {
        for (uint32_t i = 0; i < N; ++i)
        {
            SmallQueue::Item item;
            while (true)
            {
                item = queue_.allocate();
                if (item.address != nullptr)
                {
                    break;
                }
                std::this_thread::yield();
            }
            std::memcpy(item.address, &i, sizeof(i));
            item.len = sizeof(i);
            queue_.ready(item);
        }
    });

    std::thread consumer([&]()
    {
        for (uint32_t expected = 0; expected < N and not error; ++expected)
        {
            SmallQueue::Item item;
            while (true)
            {
                item = queue_.get(0ns);
                if (item.address != nullptr)
                {
                    break;
                }
                std::this_thread::yield();
            }

            uint32_t val = 0;
            std::memcpy(&val, item.address, sizeof(val));
            if (val != expected)
            {
                error = true;
            }
            queue_.free(item);
        }
    });

    producer.join();
    consumer.join();

    ASSERT_FALSE(error) << "Consumer received out-of-order data";
}

TEST_F(StressSpscQueue, slot_leak_check)
{
    // Push/pop many frames and verify all slots return to the free-stack.
    // Detects slot leaks caused by Treiber ABA or missed frees.
    constexpr uint32_t N = 50'000;

    std::thread producer([&]()
    {
        for (uint32_t i = 0; i < N; ++i)
        {
            SmallQueue::Item item;
            while (true)
            {
                item = queue_.allocate();
                if (item.address != nullptr)
                {
                    break;
                }
                std::this_thread::yield();
            }
            item.len = 4;
            queue_.ready(item);
        }
    });

    std::thread consumer([&]()
    {
        for (uint32_t i = 0; i < N; ++i)
        {
            SmallQueue::Item item;
            while (true)
            {
                item = queue_.get(0ns);
                if (item.address != nullptr)
                {
                    break;
                }
                std::this_thread::yield();
            }
            queue_.free(item);
        }
    });

    producer.join();
    consumer.join();

    // All slots should be back in the free-stack
    std::set<uint32_t> recovered;
    for (uint32_t i = 0; i < POOL_SIZE; ++i)
    {
        uint32_t idx = treiber_pop(slot_pool_.free_top, pool_, SmallQueue::SLOT_STRIDE);
        ASSERT_NE(INVALID_SLOT, idx) << "Slot leak: only recovered " << i << " of " << POOL_SIZE;
        recovered.insert(idx);
    }
    ASSERT_EQ(POOL_SIZE, recovered.size()) << "Duplicate slot indices recovered";
    ASSERT_EQ(INVALID_SLOT, treiber_pop(slot_pool_.free_top, pool_, SmallQueue::SLOT_STRIDE));
}

TEST_F(StressSpscQueue, bidirectional_stress)
{
    // Two queues sharing the same pool, two threads each producing and consuming
    // on opposite directions. Stresses the shared Treiber free-stack with 4 concurrent accessors.
    SlotPool slot_pool_b{};
    alignas(CACHE_LINE) uint8_t pool_b[POOL_SIZE * SmallQueue::SLOT_STRIDE]{};
    slot_pool_b.free_top = tagged_pack(0, INVALID_SLOT);
    for (uint32_t i = 0; i < POOL_SIZE; ++i)
    {
        treiber_push(slot_pool_b.free_top, pool_b, SmallQueue::SLOT_STRIDE, i);
    }
    SmallQueue::Context ctx_b{};
    SmallQueue queue_b{ctx_b, slot_pool_b, pool_b};
    queue_b.initContext();

    constexpr uint32_t N = 50'000;
    std::atomic<bool> error{false};

    // Thread A: produces on queue_, consumes from queue_b
    std::thread thread_a([&]()
    {
        uint32_t sent = 0;
        uint32_t expected = 0;
        while ((sent < N or expected < N) and not error)
        {
            if (sent < N)
            {
                auto item = queue_.allocate();
                if (item.address != nullptr)
                {
                    std::memcpy(item.address, &sent, sizeof(sent));
                    item.len = sizeof(sent);
                    queue_.ready(item);
                    ++sent;
                }
            }
            if (expected < N)
            {
                auto recv = queue_b.get(0ns);
                if (recv.address != nullptr)
                {
                    uint32_t val = 0;
                    std::memcpy(&val, recv.address, sizeof(val));
                    if (val != expected)
                    {
                        error = true;
                    }
                    queue_b.free(recv);
                    ++expected;
                }
            }
            std::this_thread::yield();
        }
    });

    // Thread B: produces on queue_b, consumes from queue_
    std::thread thread_b([&]()
    {
        uint32_t sent = 0;
        uint32_t expected = 0;
        while ((sent < N or expected < N) and not error)
        {
            if (sent < N)
            {
                auto item = queue_b.allocate();
                if (item.address != nullptr)
                {
                    std::memcpy(item.address, &sent, sizeof(sent));
                    item.len = sizeof(sent);
                    queue_b.ready(item);
                    ++sent;
                }
            }
            if (expected < N)
            {
                auto recv = queue_.get(0ns);
                if (recv.address != nullptr)
                {
                    uint32_t val = 0;
                    std::memcpy(&val, recv.address, sizeof(val));
                    if (val != expected)
                    {
                        error = true;
                    }
                    queue_.free(recv);
                    ++expected;
                }
            }
            std::this_thread::yield();
        }
    });

    thread_a.join();
    thread_b.join();

    ASSERT_FALSE(error) << "Bidirectional stress: data corruption or reordering detected";
}
