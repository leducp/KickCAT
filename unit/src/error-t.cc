#include <gtest/gtest.h>
#include "kickcat/Error.h"

using namespace kickcat;


TEST(Error, what)
{
    char const* ERROR_MSG = "I am an error";
    try
    {
        throw Error(ERROR_MSG);
        FAIL() << "Shall never be reached";
    }
    catch(std::exception const& e)
    {
        ASSERT_STREQ(ERROR_MSG, e.what());
    }
}

TEST(ErrorAL, category_and_code)
{
    char const* ERROR_MSG = "AL error";
    int32_t const CODE = 42;
    try
    {
        throw ErrorAL(ERROR_MSG, CODE);
        FAIL() << "Shall never be reached";
    }
    catch(ErrorAL const& e)
    {
        ASSERT_STREQ(ERROR_MSG, e.what());
        ASSERT_EQ(error::category::AL, e.category());
        ASSERT_EQ(CODE, e.code());
    }
}

TEST(ErrorCoE, category_and_code)
{
    char const* ERROR_MSG = "CoE error";
    int32_t const CODE = 0x06010000;
    try
    {
        throw ErrorCoE(ERROR_MSG, CODE);
        FAIL() << "Shall never be reached";
    }
    catch(ErrorCoE const& e)
    {
        ASSERT_STREQ(ERROR_MSG, e.what());
        ASSERT_EQ(error::category::CoE, e.category());
        ASSERT_EQ(CODE, e.code());
    }
}

TEST(ErrorDatagram, state)
{
    char const* ERROR_MSG = "datagram error";
    try
    {
        throw ErrorDatagram(ERROR_MSG, DatagramState::LOST);
        FAIL() << "Shall never be reached";
    }
    catch(ErrorDatagram const& e)
    {
        ASSERT_STREQ(ERROR_MSG, e.what());
        ASSERT_EQ(DatagramState::LOST, e.state());
    }
}

TEST(ErrorDatagram, state_invalid_wkc)
{
    char const* ERROR_MSG = "invalid wkc";
    try
    {
        throw ErrorDatagram(ERROR_MSG, DatagramState::INVALID_WKC);
        FAIL() << "Shall never be reached";
    }
    catch(ErrorDatagram const& e)
    {
        ASSERT_STREQ(ERROR_MSG, e.what());
        ASSERT_EQ(DatagramState::INVALID_WKC, e.state());
    }
}
