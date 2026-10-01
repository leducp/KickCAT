#include <gtest/gtest.h>
#include <stdexcept>

#include "kickcat/utils/xsd.h"

using namespace kickcat;

TEST(XSD, hexdec)
{
    EXPECT_EQ(xsd::parseHexDec("42"),      42);
    EXPECT_EQ(xsd::parseHexDec("+42"),     42);
    EXPECT_EQ(xsd::parseHexDec("-1"),      -1);
    EXPECT_EQ(xsd::parseHexDec(" #x1A "),  0x1A);
    EXPECT_EQ(xsd::parseHexDec("0x1a"),    0x1A);
    EXPECT_EQ(xsd::parseHexDec("#xFFFFFFFFFFFFFFFF"),   -1);   // ULINT keeps its bit pattern
    EXPECT_EQ(xsd::parseHexDec("18446744073709551615"), -1);

    for (char const* bad : {"", "-", "#x", "12abc", "1 2", "#x-1", "#x0x12", "#x 12", "0x0x12"})
    {
        EXPECT_THROW(xsd::parseHexDec(bad), std::invalid_argument) << bad;
    }
    EXPECT_THROW(xsd::parseHexDec("#x10000000000000000"), std::invalid_argument);
}

TEST(XSD, boolean)
{
    EXPECT_TRUE (xsd::parseBoolean("1"));
    EXPECT_TRUE (xsd::parseBoolean(" true "));
    EXPECT_FALSE(xsd::parseBoolean("0"));
    EXPECT_FALSE(xsd::parseBoolean("false"));
    EXPECT_THROW(xsd::parseBoolean("yes"), std::invalid_argument);
    EXPECT_THROW(xsd::parseBoolean("TRUE"), std::invalid_argument);
}

TEST(XSD, hexbinary)
{
    EXPECT_EQ(xsd::parseHexBinary("0102ff"), (std::vector<uint8_t>{0x01, 0x02, 0xff}));
    EXPECT_EQ(xsd::parseHexBinary(" 01 02\n FF "), (std::vector<uint8_t>{0x01, 0x02, 0xff}));
    EXPECT_TRUE(xsd::parseHexBinary("").empty());
    EXPECT_THROW(xsd::parseHexBinary("012"), std::invalid_argument);
    EXPECT_THROW(xsd::parseHexBinary("zz"),  std::invalid_argument);
}
