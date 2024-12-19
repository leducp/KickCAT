#include <gtest/gtest.h>
#include "kickcat/protocol.h"
#include "kickcat/CoE/protocol.h"

using namespace kickcat;
using namespace kickcat::CoE;

// no real logic here, just check that code to string functions returns valid result (right formed C string with a few caracters)

TEST(CoE, SDO_abort_to_str)
{
    for (uint32_t i = 0x05000000; i < 0x05050000; ++i)
    {
        char const* text = CoE::SDO::abort_to_str(i);
        ASSERT_EQ(5, strnlen(text, 5));
    }

    for (uint32_t i = 0x06000000; i < 0x060B0000; ++i)
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

TEST(CoE, SDO_header_to_str)
{
    {
        CoE::Header header{};
        std::string str = toString(&header);
        ASSERT_LT(15, str.size());
    }

    {
        CoE::Header header{};
        header.service = Service::TxPDO_REMOTE_REQUEST;
        std::string str = toString(&header);
        ASSERT_LT(12, str.size());
    }

    {
        uint8_t raw_message[128];
        auto* header = pointData<CoE::Header>(raw_message);
        auto* sdo    = pointData<CoE::ServiceData>(header);
        header->service = Service::SDO_REQUEST;
        sdo->index = 0x1018;
        sdo->subindex = 3;
        sdo->command = SDO::request::UPLOAD;
        std::string str = toString(header);
        ASSERT_LT(30, str.size());
    }
}

TEST(CoE, SDO_request_command_to_string)
{
    ASSERT_STREQ(SDO::request::toString(0), "download segmented");
    ASSERT_STREQ(SDO::request::toString(1), "download");
    ASSERT_STREQ(SDO::request::toString(2), "upload");
    ASSERT_STREQ(SDO::request::toString(3), "upload segmented");
    ASSERT_STREQ(SDO::request::toString(4), "abort");

    for (uint8_t i = 5; i < UINT8_MAX; ++i)
    {
        ASSERT_STREQ(SDO::request::toString(i), "Unknown command");
    }
}
