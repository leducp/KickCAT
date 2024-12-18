#include <gtest/gtest.h>
#include "kickcat/CoE/protocol.h"

using namespace kickcat;
using namespace kickcat::CoE;

// no real logic here, just check that code to string functions returns valid result (right formed C string with a few caracters)

TEST(Protocol, SDO_abort_to_str)
{
    for (uint32_t i = 0x05000000; i < 0x05050000; ++i)
    {
        char const* text = CoE::SDO::abort_to_str(i);
        ASSERT_EQ(5, strnlen(text, 5));
    }

    for (uint32_t i = 0x06000000; i < 0x060A0000; ++i)
    {
        char const* text = CoE::SDO::abort_to_str(i);
        ASSERT_EQ(5, strnlen(text, 5));
    }

    for (uint32_t i = 0x08000000; i < 0x08001000; ++i)
    {
        char const* text = CoE::SDO::abort_to_str(i);
        ASSERT_EQ(5, strnlen(text, 5));
    }
}

