#include <gtest/gtest.h>
#include "kickcat/protocol.h"

using namespace kickcat;

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


TEST(Protocol, hton)
{
    constexpr uint16_t host_16 = 0xCAFE;
    constexpr uint32_t host_32 = 0xCAFEDECA;
    constexpr uint32_t host_64 = 0;

    uint16_t network_16 = hton<uint16_t>(host_16);
    uint32_t network_32 = hton<uint32_t>(host_32);

    ASSERT_EQ(0xFECA,     network_16);
    ASSERT_EQ(0xCADEFECA, network_32);
    ASSERT_THROW(hton<uint64_t>(host_64), kickcat::Error);
}