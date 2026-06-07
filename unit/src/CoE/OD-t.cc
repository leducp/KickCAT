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
    CoE::addEntry<uint8_t>(object,0,8,0,Access::READ,DataType::UNSIGNED8,"Subindex 000",0x4);
    CoE::addEntry<uint32_t>(object,1,32,16,Access::READ,DataType::UNSIGNED32,"Vendor ID",0x6a5);
    CoE::addEntry<uint32_t>(object,2,32,48,Access::READ,DataType::UNSIGNED32,"Product code",0xb0cad0);
    CoE::addEntry<uint32_t>(object,3,32,80,Access::READ,DataType::UNSIGNED32,"Revision number",0x0);
    CoE::addEntry<uint32_t>(object,4,32,112,Access::READ,DataType::UNSIGNED32,"Serial number",0xcafedeca);

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

TEST(OD, validate_dictionary_accepts_well_formed)
{
    Dictionary dict;
    {
        CoE::Object object{0x1018, CoE::ObjectCode::RECORD, "Identity Object", {}};
        CoE::addEntry<uint8_t> (object, 0, 8,  0, Access::READ, DataType::UNSIGNED8,  "Subindex 000", 0x4);
        CoE::addEntry<uint32_t>(object, 1, 32, 8, Access::READ, DataType::UNSIGNED32, "Vendor ID",    0x6a5);
        dict.push_back(std::move(object));
    }

    auto problems = validateDictionary(dict);
    ASSERT_TRUE(problems.empty()) << problems.front();
}

TEST(OD, validate_dictionary_flags_readable_entry_with_null_data)
{
    Dictionary dict;
    {
        CoE::Object object{0x6000, CoE::ObjectCode::VAR, "Readable VAR without storage", {}};
        // nullptr overload: a readable entry with NO backing buffer -> SDO upload would deref null
        CoE::addEntry(object, 0, 16, 0, Access::READ, DataType::UNSIGNED16, "no data", nullptr);
        dict.push_back(std::move(object));
    }

    auto problems = validateDictionary(dict);
    ASSERT_EQ(1u, problems.size());
}

TEST(OD, validate_dictionary_flags_writable_entry_with_null_data)
{
    Dictionary dict;
    {
        CoE::Object object{0x7000, CoE::ObjectCode::VAR, "Writable VAR without storage", {}};
        CoE::addEntry(object, 0, 16, 0, Access::WRITE, DataType::UNSIGNED16, "no data", nullptr);
        dict.push_back(std::move(object));
    }

    auto problems = validateDictionary(dict);
    ASSERT_EQ(1u, problems.size());
}

TEST(OD, validate_dictionary_flags_complex_object_without_subindex0)
{
    Dictionary dict;
    {
        CoE::Object object{0x1A00, CoE::ObjectCode::ARRAY, "Array missing subindex 0", {}};
        dict.push_back(std::move(object));
    }

    auto problems = validateDictionary(dict);
    ASSERT_EQ(1u, problems.size());
}

TEST(OD, validate_dictionary_ignores_inaccessible_entry_with_null_data)
{
    // A padding/non-SDO entry (access == 0) is never served, so null data is acceptable.
    Dictionary dict;
    {
        CoE::Object object{0x1600, CoE::ObjectCode::VAR, "Padding", {}};
        CoE::addEntry(object, 0, 16, 0, 0, DataType::UNSIGNED16, "pad", nullptr);
        dict.push_back(std::move(object));
    }

    auto problems = validateDictionary(dict);
    ASSERT_TRUE(problems.empty());
}

TEST(OD, materialize_storage_allocates_accessible_entries_zeroed)
{
    Dictionary dict;
    {
        CoE::Object object{0x6000, CoE::ObjectCode::VAR, "Input", {}};
        CoE::addEntry(object, 0, 1,  0, Access::READ,  DataType::BOOLEAN,    "bit",  nullptr);
        CoE::addEntry(object, 1, 16, 8, Access::WRITE, DataType::UNSIGNED16, "word", nullptr);
        dict.push_back(std::move(object));
    }

    ASSERT_FALSE(validateDictionary(dict).empty());  // null data before materialization

    materializeStorage(dict);

    auto& entries = dict.front().entries;
    ASSERT_NE(entries[0].data, nullptr);  // BOOL (bitlen 1) gets 1 byte, not 0
    ASSERT_NE(entries[1].data, nullptr);
    ASSERT_EQ(*static_cast<uint8_t const*>(entries[0].data),  0);  // zero-initialized
    ASSERT_EQ(*static_cast<uint16_t const*>(entries[1].data), 0);
    ASSERT_TRUE(validateDictionary(dict).empty());
}

TEST(OD, materialize_storage_preserves_existing_data_and_skips_inaccessible)
{
    Dictionary dict;
    {
        CoE::Object object{0x1018, CoE::ObjectCode::VAR, "Identity", {}};
        CoE::addEntry<uint32_t>(object, 0, 32, 0, Access::READ, DataType::UNSIGNED32, "has value", 0xCAFEu);
        CoE::addEntry(object, 1, 16, 32, 0, DataType::UNSIGNED16, "padding", nullptr);  // access == 0
        dict.push_back(std::move(object));
    }

    void const* existing = dict.front().entries[0].data;
    materializeStorage(dict);

    ASSERT_EQ(dict.front().entries[0].data, existing);                 // untouched
    ASSERT_EQ(*static_cast<uint32_t const*>(dict.front().entries[0].data), 0xCAFEu);
    ASSERT_EQ(dict.front().entries[1].data, nullptr);                  // inaccessible: left null
}

TEST(OD, materialize_storage_allocates_complex_subindex0)
{
    Dictionary dict;
    {
        // ARRAY whose count (subindex 0) has access 0: complete access still dereferences it.
        CoE::Object object{0x1C12, CoE::ObjectCode::ARRAY, "RxPDO assign", {}};
        CoE::addEntry(object, 0, 8, 0, 0, DataType::UNSIGNED8, "count", nullptr);
        dict.push_back(std::move(object));
    }

    materializeStorage(dict);

    ASSERT_NE(dict.front().entries.front().data, nullptr);
    ASSERT_TRUE(validateDictionary(dict).empty());
}
