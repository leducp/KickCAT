#include <gtest/gtest.h>
#include "kickcat/protocol.h"

using namespace kickcat;

// no real logic here, just check that code to string functions returns valid result (right formed C string with a few caracters)

TEST(Protocol, SDO_abort_to_str)
{
    for (uint32_t i = 0x05000000; i < 0x08001000; ++i)
    {
        char const* text = CoE::SDO::abort_to_str(i);
        ASSERT_EQ(5, strnlen(text, 5));
    }
}

TEST(Protocol, ALStatus_to_string)
{
    for (int32_t i = 0; i < UINT16_MAX; ++i) // AL status code is defined on 16bits
    {
        char const* text = ALStatus_to_string(i);
        ASSERT_EQ(5, strnlen(text, 5));
    }
}


TEST(Protocol, State_to_string)
{
    for (uint8_t i = 0; i < UINT8_MAX; ++i) // AL status code is defined on 16bits
    {
        char const* text = toString(static_cast<State>(i));
        ASSERT_EQ(4, strnlen(text, 4));
    }
}
