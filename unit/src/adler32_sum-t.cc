#include <gtest/gtest.h>
#include "kickcat/checksum/adler32.h"

using namespace kickcat;

TEST(Adler, nominal)
{
    uint8_t data[4096];
    memset(data, 0xFF, 4096);

    uint32_t res1 = adler32Sum(data, 4096);
    uint32_t res2 = adler32Sum(data, 4096);
    data[0] = 0xFE;
    uint32_t res3 = adler32Sum(data, 4096);
    ASSERT_EQ(res1, res2);
    ASSERT_NE(res1, res3);
}

TEST(Adler, consistent_implementation)
{
    std::string hello = "hello world";
    uint32_t expected = 0x1A0B045D;

    ASSERT_EQ(expected, adler32Sum(hello.data(), hello.size()));
}
