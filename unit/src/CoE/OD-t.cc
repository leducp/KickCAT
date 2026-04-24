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

TEST(OD, addEntry_sub_byte_allocates_one_byte)
{
    Object object{0x6000, ObjectCode::VAR, "GPIOs", {}};

    addEntry<uint8_t>(object, 1, 1, 0, Access::READ, DataType::BOOLEAN, "GPIO 1", uint8_t{1});
    addEntry<uint8_t>(object, 2, 1, 1, Access::READ, DataType::BOOLEAN, "GPIO 2", uint8_t{0});
    addEntry<uint8_t>(object, 3, 1, 2, Access::READ, DataType::BOOLEAN, "GPIO 3", uint8_t{1});
    addEntry<uint8_t>(object, 4, 6, 3, Access::READ, DataType::BIT6,    "BIT6",   uint8_t{0x2A});

    ASSERT_EQ(object.entries.size(), 4);
    for (auto const& entry : object.entries)
    {
        ASSERT_NE(entry.data, nullptr);
        ASSERT_EQ(entry.data_bit_offset, 0);
    }

    EXPECT_EQ(*static_cast<uint8_t*>(object.entries[0].data), 0x01);
    EXPECT_EQ(*static_cast<uint8_t*>(object.entries[1].data), 0x00);
    EXPECT_EQ(*static_cast<uint8_t*>(object.entries[2].data), 0x01);
    EXPECT_EQ(*static_cast<uint8_t*>(object.entries[3].data), 0x2A);
}

TEST(OD, copyBits_byte_aligned_is_memcpy)
{
    uint8_t src[4] = {0x11, 0x22, 0x33, 0x44};
    uint8_t dst[4] = {0xAA, 0xBB, 0xCC, 0xDD};

    copyBits(src, 0, dst, 0, 32);

    EXPECT_EQ(dst[0], 0x11);
    EXPECT_EQ(dst[1], 0x22);
    EXPECT_EQ(dst[2], 0x33);
    EXPECT_EQ(dst[3], 0x44);
}

TEST(OD, copyBits_single_bit_preserves_neighbours)
{
    uint8_t src = 0x01;
    uint8_t dst = 0xA5;

    copyBits(&src, 0, &dst, 5, 1);
    EXPECT_EQ(dst & (1 << 5), (1 << 5));
    EXPECT_EQ(dst & ~uint8_t(1 << 5), 0xA5 & ~uint8_t(1 << 5));

    src = 0x00;
    dst = 0xFF;
    copyBits(&src, 0, &dst, 3, 1);
    EXPECT_EQ(dst & (1 << 3), 0);
    EXPECT_EQ(dst | uint8_t(1 << 3), 0xFF);
}

TEST(OD, copyBits_cross_byte)
{
    // 4 bits src[6..9] -> dst[5..8]
    uint8_t src[2] = {0xC0, 0x03};
    uint8_t dst[2] = {0x00, 0x00};

    copyBits(src, 6, dst, 5, 4);
    EXPECT_EQ(dst[0] & 0xE0, 0xE0);
    EXPECT_EQ(dst[0] & 0x1F, 0x00);
    EXPECT_EQ(dst[1] & 0x01, 0x01);
    EXPECT_EQ(dst[1] & 0xFE, 0x00);
}

TEST(OD, copyBits_spans_three_source_bytes)
{
    // 16 bits src[6..21] -> dst[0..15], LSB-first.
    uint8_t src[3] = {0xC0, 0xAA, 0x03};
    uint8_t dst[2] = {0xFF, 0xFF};

    copyBits(src, 6, dst, 0, 16);

    EXPECT_EQ(dst[0], 0xAB);
    EXPECT_EQ(dst[1], 0x0E);
}

TEST(OD, readEntryBits_writeEntryBits_roundtrip)
{
    // BOOL aliased at bit 3 of a buffer byte.
    uint8_t aliased_buffer = 0xA5;
    Entry entry;
    entry.bitlen           = 1;
    entry.bitoff           = 0;
    entry.type             = DataType::BOOLEAN;
    entry.data             = &aliased_buffer;
    entry.is_mapped        = true;
    entry.data_bit_offset  = 3;

    uint8_t wire = 0x00;
    readEntryBits(&entry, &wire, 0);
    EXPECT_EQ(wire & 0x01, (0xA5 >> 3) & 0x01);

    wire = 0x01;
    writeEntryBits(&entry, &wire, 0);
    EXPECT_EQ(aliased_buffer & (1 << 3), (1 << 3));
    EXPECT_EQ(aliased_buffer & ~uint8_t(1 << 3), 0xA5 & ~uint8_t(1 << 3));

    // Defuse alias: Entry destructor would free our stack byte otherwise.
    entry.data = nullptr;
    entry.is_mapped = false;
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
