#include <gtest/gtest.h>
#include <cstring>
#include "mocks/Time.h"

#include "kickcat/SBufQueue.h"

using namespace kickcat;
using SBUF_QUEUE = SBufQueue<uint8_t[1522], 64>;

class TestSBufQueue :  public ::testing::Test
{
public:
    void SetUp() override
    {
        queue_.initContext();

        ASSERT_EQ(SBUF_QUEUE::depth(), queue_.freed());
        ASSERT_EQ(SBUF_QUEUE::item_size(), 1522);
        ASSERT_EQ(0, queue_.readied());

        resetSinceEpoch(); // reset since_epoch() mocked starting point
    }

    SBUF_QUEUE::Context context_;
    SBUF_QUEUE queue_{context_};
};

TEST_F(TestSBufQueue, push_pop_nominal)
{
    uint32_t const PAYLOAD = 42;

    // Push a payload
    {
        auto item = queue_.allocate(0ns);
        std::memcpy(item.address, &PAYLOAD, sizeof(PAYLOAD));
        ASSERT_NE(SBUF_INVALID_INDEX, item.index);
        queue_.ready(item);
        ASSERT_EQ(1, queue_.readied());
    }

    // Consume a payload
    {
        auto item = queue_.get(0ns);
        ASSERT_EQ(0, std::memcmp(item.address, &PAYLOAD, sizeof(PAYLOAD)));
        queue_.free(item);
        ASSERT_EQ(0, queue_.readied());
    }
}

TEST_F(TestSBufQueue, nothing_to_read)
{
    ASSERT_EQ(0, queue_.readied());

    auto item = queue_.get(0ns);
    ASSERT_EQ(SBUF_INVALID_INDEX, item.index);
}
