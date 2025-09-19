#include <gtest/gtest.h>
#include "kickcat/KickCAT.h"

using namespace kickcat;

TEST(KickCAT, datagram_state_to_string)
{
    std::array<char const*, 6> TO_CHECK = {"LOST", "SEND_ERROR", "INVALID_WKC", "NO_HANDLER", "OK", "Unknown"};
    for (int i = 0; i < 6; ++i)
    {
        ASSERT_STREQ(TO_CHECK.at(i), toString(static_cast<DatagramState>(i)));
    }
}
