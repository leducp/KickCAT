#include <gtest/gtest.h>
#include "kickcat/KickCAT.h"

using namespace kickcat;

TEST(KickCAT, datagram_state_to_string)
{
    ASSERT_STREQ("LOST"       , toString(DatagramState::LOST));
    ASSERT_STREQ("SEND_ERROR" , toString(DatagramState::SEND_ERROR));
    ASSERT_STREQ("INVALID_WKC", toString(DatagramState::INVALID_WKC));
    ASSERT_STREQ("NO_HANDLER" , toString(DatagramState::NO_HANDLER));
    ASSERT_STREQ("OK"         , toString(DatagramState::OK));
    ASSERT_STREQ("Unknown"    , toString(static_cast<DatagramState>(42)));
}
