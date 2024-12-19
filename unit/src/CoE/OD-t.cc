#include <gtest/gtest.h>

#include "kickcat/CoE/OD.h"

using namespace kickcat;
using namespace kickcat::CoE;

// Test OD prints: mostly no real logic, just checking that nothing crash and that strings are not empty

TEST(CoE, object_code_to_string)
{
    ASSERT_STREQ(toString(ObjectCode::NIL),        "NULL");
    ASSERT_STREQ(toString(ObjectCode::DOMAIN),     "DOMAIN");
    ASSERT_STREQ(toString(ObjectCode::DEFTYPE),    "DEFTYPE");
    ASSERT_STREQ(toString(ObjectCode::DEFSTRUCT),  "DEFSTRUCT");
    ASSERT_STREQ(toString(ObjectCode::VAR),        "VAR");
    ASSERT_STREQ(toString(ObjectCode::ARRAY),      "ARRAY");
    ASSERT_STREQ(toString(ObjectCode::RECORD),     "RECORD");

    for (uint8_t i = 0; i < UINT8_MAX; ++i)
    {
        std::string str = toString(ObjectCode(i));
        ASSERT_LT(2, str.size());
    }
}

TEST(CoE, data_type_to_string)
{
    for (uint16_t i = 0; i < UINT16_MAX; ++i)
    {
        std::string str = toString(DataType(i));
        ASSERT_LT(3, str.size());
    }
}

TEST(CoE, access_to_string)
{
    {
        std::string access = Access::toString(0);
        ASSERT_EQ("", access);
    }

    {
        std::string access = Access::toString(Access::READ | Access::WRITE);
        ASSERT_EQ("read(PreOP,SafeOP,OP), write(PreOP,SafeOP,OP)", access);
    }

    {
        std::string access = Access::toString(Access::READ_OP | Access::WRITE_OP);
        ASSERT_EQ("read(OP), write(OP)", access);
    }

    {
        std::string access = Access::toString(Access::READ_PREOP | Access::TxPDO | Access::BACKUP);
        ASSERT_EQ("read(PreOP), TxPDO, Backup", access);
    }

    {
        std::string access = Access::toString(Access::WRITE_PREOP | Access::RxPDO | Access::SETTING);
        ASSERT_EQ("write(PreOP), RxPDO, Setting", access);
    }
}

TEST(OD, entry_data_to_string)
{
    {
        Entry entry;
        entry.data = std::malloc(sizeof(int32_t));
        entry.type = DataType::INTEGER32;
        int32_t data = -80032;
        std::memcpy(entry.data, &data, sizeof(int32_t));

        std::string str = entry.dataToString();
        ASSERT_EQ("-80032", str);
    }

    {
        Entry entry;
        entry.data = std::malloc(sizeof(uint32_t));
        entry.type = DataType::UNSIGNED32;
        uint32_t data = 80032;
        std::memcpy(entry.data, &data, sizeof(uint32_t));

        std::string str = entry.dataToString();
        ASSERT_EQ("80032", str);
    }

    {
        Entry entry;
        entry.data = std::malloc(sizeof(uint16_t));
        entry.type = DataType::UNSIGNED16;
        uint16_t data = 3042;
        std::memcpy(entry.data, &data, sizeof(uint16_t));

        std::string str = entry.dataToString();
        ASSERT_EQ("3042", str);
    }

    {
        Entry entry;
        entry.data = std::malloc(sizeof(int16_t));
        entry.type = DataType::INTEGER16;
        int16_t data = -3042;
        std::memcpy(entry.data, &data, sizeof(int16_t));

        std::string str = entry.dataToString();
        ASSERT_EQ("-3042", str);
    }

    {
        Entry entry;
        entry.data = std::malloc(sizeof(uint64_t));
        entry.type = DataType::UNSIGNED64;
        uint64_t data = 5000000000;
        std::memcpy(entry.data, &data, sizeof(uint64_t));

        std::string str = entry.dataToString();
        ASSERT_EQ("5000000000", str);
    }

    {
        Entry entry;
        entry.data = std::malloc(sizeof(int64_t));
        entry.type = DataType::INTEGER64;
        int64_t data = -5000000000;
        std::memcpy(entry.data, &data, sizeof(int64_t));

        std::string str = entry.dataToString();
        ASSERT_EQ("-5000000000", str);
    }

    {
        Entry entry;
        entry.data = std::malloc(sizeof(float));
        entry.type = DataType::REAL32;
        float data = 3.14;
        std::memcpy(entry.data, &data, sizeof(float));

        std::string str = entry.dataToString();
        ASSERT_EQ("3.14", str);
    }

    {
        Entry entry;
        entry.data = std::malloc(sizeof(double));
        entry.type = DataType::REAL64;
        double data = 3.141592653589793;
        std::memcpy(entry.data, &data, sizeof(double));

        std::string str = entry.dataToString();
        ASSERT_EQ("3.141592653589793", str);
    }

    {
        Entry entry;
        std::string data = "Hello World from an object";
        entry.bitlen = data.size() * 8;
        entry.data = std::malloc(data.size());
        entry.type = DataType::VISIBLE_STRING;
        std::memcpy(entry.data, data.data(), data.size());

        std::string str = entry.dataToString();
        ASSERT_EQ(data, str);
    }
}

TEST(OD, print_object_and_entries)
{
    CoE::Object object
    {
        0x1018,
        CoE::ObjectCode::RECORD,
        "Identity Object",
        {}
    };
    CoE::addEntry<uint8_t>(object,0,8,0,7,static_cast<CoE::DataType>(5),"Subindex 000",0x4);
    CoE::addEntry<uint32_t>(object,1,32,16,7,static_cast<CoE::DataType>(7),"Vendor ID",0x6a5);
    CoE::addEntry<uint32_t>(object,2,32,48,7,static_cast<CoE::DataType>(7),"Product code",0xb0cad0);
    CoE::addEntry<uint32_t>(object,3,32,80,7,static_cast<CoE::DataType>(7),"Revision number",0x0);
    CoE::addEntry<uint32_t>(object,4,32,112,7,static_cast<CoE::DataType>(7),"Serial number",0xcafedeca);

    Entry entry_null_data;
    entry_null_data.data = nullptr;
    object.entries.push_back(std::move(entry_null_data));

    Entry no_rendered;
    no_rendered.type = DataType::BIT11;
    no_rendered.data = std::malloc(1);
    object.entries.push_back(std::move(no_rendered));

    std::string str = toString(object);
    ASSERT_LT(1100, str.size());
}
