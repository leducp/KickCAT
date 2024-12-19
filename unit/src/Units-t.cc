#include <gtest/gtest.h>

#include "kickcat/Units.h"

using namespace kickcat;

TEST(Units, bytes)
{
    ASSERT_EQ(1_KiB, 1024);
    ASSERT_EQ(1_MiB, 1_KiB * 1024);
    ASSERT_EQ(1_GiB, 1_MiB * 1024);
    ASSERT_EQ(1_TiB, 1_GiB * 1024);
}
