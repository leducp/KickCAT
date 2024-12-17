#include <gtest/gtest.h>

#include "kickcat/Ring.h"

using namespace kickcat;

constexpr int RING_TEST_SIZE = 32;

class RingTest : public testing::Test
{
public:
    void SetUp() override
    {
        ring.reset();
        ASSERT_EQ(ring.size(), 0);
        ASSERT_EQ(ring.available(), RING_TEST_SIZE);
        ASSERT_TRUE (ring.isEmpty());
        ASSERT_FALSE(ring.isFull());
    }

    void TearDown() override
    {

    }

    Ring<int, RING_TEST_SIZE>::Context ctx;
    Ring<int, RING_TEST_SIZE> ring{ctx};
};

TEST_F(RingTest, push_until_full_then_empty)
{
    int i = 1;
    while (ring.push(i))
    {
        ASSERT_EQ(ring.available(), RING_TEST_SIZE - i);
        ASSERT_EQ(ring.size(), i);
        i++;
    }
    ASSERT_EQ(i - 1, RING_TEST_SIZE);
    ASSERT_TRUE (ring.isFull());
    ASSERT_FALSE(ring.isEmpty());
    ASSERT_EQ(ring.available(), 0);
    ASSERT_EQ(ring.size(), RING_TEST_SIZE);

    while (ring.pop(i))
    {
        ASSERT_EQ(ring.available(), i);
        ASSERT_EQ(ring.size(), RING_TEST_SIZE - i);
    }
}
