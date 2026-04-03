#include <gtest/gtest.h>

#include "kickmsg/types.h"

TEST(Helpers, AlignUp)
{
    EXPECT_EQ(kickmsg::align_up(0, 64), 0u);
    EXPECT_EQ(kickmsg::align_up(1, 64), 64u);
    EXPECT_EQ(kickmsg::align_up(64, 64), 64u);
    EXPECT_EQ(kickmsg::align_up(65, 64), 128u);
    EXPECT_EQ(kickmsg::align_up(127, 128), 128u);
}

TEST(Helpers, IsPowerOfTwo)
{
    EXPECT_FALSE(kickmsg::is_power_of_two(0));
    EXPECT_TRUE(kickmsg::is_power_of_two(1));
    EXPECT_TRUE(kickmsg::is_power_of_two(2));
    EXPECT_FALSE(kickmsg::is_power_of_two(3));
    EXPECT_TRUE(kickmsg::is_power_of_two(64));
    EXPECT_FALSE(kickmsg::is_power_of_two(100));
    EXPECT_TRUE(kickmsg::is_power_of_two(1024));
}

TEST(Helpers, TaggedPackRoundtrip)
{
    for (uint32_t gen : {0u, 1u, 42u, UINT32_MAX})
    {
        for (uint32_t idx : {0u, 1u, 255u, kickmsg::INVALID_SLOT})
        {
            auto packed = kickmsg::tagged_pack(gen, idx);
            EXPECT_EQ(kickmsg::tagged_gen(packed), gen);
            EXPECT_EQ(kickmsg::tagged_idx(packed), idx);
        }
    }
}
