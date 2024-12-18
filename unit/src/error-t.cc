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
