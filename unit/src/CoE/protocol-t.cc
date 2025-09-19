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


TEST(CoE, SDO_Information_ValueInfo_to_string)
{
    using namespace SDO::information;

    ASSERT_EQ(ValueInfo::toString(0), "None");

    ASSERT_EQ(ValueInfo::toString(ValueInfo::UNIT_TYPE), "Unit Type");
    ASSERT_EQ(ValueInfo::toString(ValueInfo::DEFAULT), "Default");
    ASSERT_EQ(ValueInfo::toString(ValueInfo::MINIMUM), "Minimum");
    ASSERT_EQ(ValueInfo::toString(ValueInfo::MAXIMUM), "Maximum");

    ASSERT_EQ(ValueInfo::toString(ValueInfo::UNIT_TYPE | ValueInfo::DEFAULT | ValueInfo::MINIMUM | ValueInfo::MAXIMUM),
        "Unit Type, Default, Minimum, Maximum");
}


TEST(CoE, SDO_Information_ObjectDescription_to_string)
{
    using namespace SDO::information;
    ObjectDescription desc;
    desc.index = 0x1018;
    desc.data_type = DataType::DWORD;
    desc.max_subindex = 1;
    desc.object_code = ObjectCode::VAR;

    char const* EXPECTED =
    "Object Description \n"
    "  index:        0x1018\n"
    "  data type:    dword\n"
    "  max subindex: 1\n"
    "  object code:  VAR\n";

    ASSERT_EQ(toString(desc), EXPECTED);
}


TEST(CoE, SDO_Information_EntryDescription_to_string)
{
    using namespace SDO::information;
    EntryDescription desc;
    desc.index = 0x1018;
    desc.subindex = 42;
    desc.value_info = 0;
    desc.data_type = DataType::BYTE;
    desc.bit_length = 8;
    desc.access = Access::ALL;

    char const* EXPECTED =
    "Entry Description \n"
    "  index:         0x1018\n"
    "  subindex:      0x2a\n"
    "  value info:    None\n"
    "  data type:     byte\n"
    "  bit length:    8\n"
    "  object access:read(PreOP,SafeOP,OP), write(PreOP,SafeOP,OP), RxPDO, TxPDO, Backup, Setting\n";

    ASSERT_EQ(toString(desc), EXPECTED);
}
