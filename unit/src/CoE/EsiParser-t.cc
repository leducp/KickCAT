#include <gtest/gtest.h>
#include <algorithm>

#include "kickcat/CoE/EsiParser.h"

// This test is not a real unit test but an integration test: it could be rework later
// if the benefit/cost is OK

using namespace kickcat;

TEST(EsiParser, load_error_not_an_xml)
{
    CoE::EsiParser parser;
    ASSERT_THROW((void) parser.loadFirstDictionaryFromFile(""),   std::runtime_error);
    ASSERT_THROW((void) parser.loadString(""), std::runtime_error);
}

TEST(EsiParser, load_error_xml_not_an_esi)
{
    CoE::EsiParser parser;
    ASSERT_THROW((void) parser.loadString("<test></test>"), std::invalid_argument);
}


TEST(EsiParser, load)
{
    CoE::EsiParser parser;
    auto dictionary = parser.loadFirstDictionaryFromFile("foot.xml");

    ASSERT_EQ(dictionary.size(), 22);
    ASSERT_STREQ(parser.profile(), "0");
    ASSERT_STREQ(parser.vendor(),  "Wandercraft");

    {
        auto [object, entry] = findObject(dictionary, 0x1000, 0);
        ASSERT_NE(object, nullptr);
        ASSERT_NE(entry,  nullptr);

        ASSERT_EQ(object->index, 0x1000);
        ASSERT_EQ(object->code, CoE::ObjectCode::VAR);
        ASSERT_EQ(object->name, "Device Type");
        ASSERT_EQ(object->entries.size(), 1);

        ASSERT_EQ(entry->subindex, 0);
        ASSERT_EQ(entry->type,   CoE::DataType::UNSIGNED32);
        ASSERT_EQ(entry->bitlen, 32);
        ASSERT_EQ(entry->bitoff, 0);
        ASSERT_EQ(entry->access, CoE::Access::READ);

        uint32_t data;
        std::memcpy(&data, entry->data, 4);
        ASSERT_EQ(data, 0);
    }

    {
        auto [object, entry] = findObject(dictionary, 0x1018, 0);
        ASSERT_NE(object, nullptr);
        ASSERT_NE(entry,  nullptr);

        ASSERT_EQ(object->index, 0x1018);
        ASSERT_EQ(CoE::ObjectCode::RECORD, object->code);
        ASSERT_EQ(object->name, "Identity Object");
        ASSERT_EQ(object->entries.size(), 5);
    }

    {
        auto [object, entry] = findObject(dictionary, 0x1600, 0);
        ASSERT_NE(object, nullptr);
        ASSERT_NE(entry,  nullptr);

        ASSERT_EQ(object->index, 0x1600);
        ASSERT_EQ(CoE::ObjectCode::RECORD, object->code);
        ASSERT_EQ(object->name, "RxPDO Map 1");
        ASSERT_EQ(object->entries.size(), 2);
    }

    {
        auto [object, entry] = findObject(dictionary, 0x1A00, 0);
        ASSERT_NE(object, nullptr);
        ASSERT_NE(entry,  nullptr);

        ASSERT_EQ(object->index, 0x1A00);
        ASSERT_EQ(CoE::ObjectCode::RECORD, object->code);
        ASSERT_EQ(object->name, "TxPDO Map 1");
        ASSERT_EQ(object->entries.size(), 15);
    }

    {
        auto [object, entry] = findObject(dictionary, 0x1C00, 0);
        ASSERT_NE(object, nullptr);
        ASSERT_NE(entry,  nullptr);

        ASSERT_EQ(object->index, 0x1C00);
        ASSERT_EQ(CoE::ObjectCode::ARRAY, object->code);
        ASSERT_EQ(object->name, "Sync manager type");
        ASSERT_EQ(object->entries.size(), 5);
    }

    {
        auto [object, entry] = findObject(dictionary, 0x1C12, 0);
        ASSERT_NE(object, nullptr);
        ASSERT_NE(entry,  nullptr);

        ASSERT_EQ(CoE::ObjectCode::ARRAY, object->code);
    }

    {
        auto [object, entry] = findObject(dictionary, 0x1C13, 0);
        ASSERT_NE(object, nullptr);
        ASSERT_NE(entry,  nullptr);

        ASSERT_EQ(CoE::ObjectCode::ARRAY, object->code);
    }

    {
        auto [object, entry] = findObject(dictionary, 0x2000, 0);
        ASSERT_NE(object, nullptr);
        ASSERT_NE(entry,  nullptr);

        ASSERT_EQ(CoE::ObjectCode::RECORD, object->code);
    }

    for (int i = 0; i < 0xe; ++i)
    {
        auto [object, entry] = findObject(dictionary, 0x6000 + i, 0);
        ASSERT_NE(object, nullptr);
        ASSERT_NE(entry,  nullptr);

        ASSERT_EQ(object->index, 0x6000 + i);
        ASSERT_EQ(object->code, CoE::ObjectCode::VAR);
        ASSERT_EQ(object->entries.size(), 1);

        ASSERT_EQ(entry->subindex, 0);
        ASSERT_EQ(entry->type,   CoE::DataType::UNSIGNED16);
        ASSERT_EQ(entry->bitlen, 16);
        ASSERT_EQ(entry->bitoff, 0);
    }
}

TEST(EsiParser, load_complex_with_enums_and_default_value)
{
    CoE::EsiParser parser;
    auto dictionary = parser.loadFirstDictionaryFromFile("kickcat_esi_test_complex.xml");

    ASSERT_STREQ(parser.profile(), "5002");
    ASSERT_STREQ(parser.vendor(),  "KickCAT");

    // 5 objects + 1 SM type object = 6
    ASSERT_EQ(dictionary.size(), 6);

    // 0x1000 - standard VAR with DefaultData
    {
        auto [object, entry] = findObject(dictionary, 0x1000, 0);
        ASSERT_NE(object, nullptr);
        ASSERT_NE(entry,  nullptr);

        ASSERT_EQ(object->code, CoE::ObjectCode::VAR);
        ASSERT_EQ(entry->type, CoE::DataType::UNSIGNED32);
        ASSERT_EQ(entry->bitlen, 32);
        ASSERT_NE(entry->data, nullptr);

        uint32_t data;
        std::memcpy(&data, entry->data, 4);
        ASSERT_EQ(data, 0x1389);
    }

    // 0x6010 - VAR using enum type (DT_OperationMode -> USINT) with DefaultValue (decimal)
    {
        auto [object, entry] = findObject(dictionary, 0x6010, 0);
        ASSERT_NE(object, nullptr);
        ASSERT_NE(entry,  nullptr);

        ASSERT_EQ(object->code, CoE::ObjectCode::VAR);
        ASSERT_EQ(object->name, "Operation Mode");
        ASSERT_EQ(entry->type, CoE::DataType::UNSIGNED8);
        ASSERT_EQ(entry->bitlen, 8);
        ASSERT_NE(entry->data, nullptr);

        uint8_t data;
        std::memcpy(&data, entry->data, 1);
        ASSERT_EQ(data, 2);
    }

    // 0x6020 - VAR using leaf alias type (DT_Temperature -> INT) with DefaultValue (hex)
    {
        auto [object, entry] = findObject(dictionary, 0x6020, 0);
        ASSERT_NE(object, nullptr);
        ASSERT_NE(entry,  nullptr);

        ASSERT_EQ(object->code, CoE::ObjectCode::VAR);
        ASSERT_EQ(object->name, "Temperature");
        ASSERT_EQ(entry->type, CoE::DataType::INTEGER16);
        ASSERT_EQ(entry->bitlen, 16);
        ASSERT_NE(entry->data, nullptr);

        int16_t data;
        std::memcpy(&data, entry->data, 2);
        ASSERT_EQ(data, 0x00E1);
    }

    // 0x2000 - RECORD with enum and alias SubItems, DefaultValue in SubItems
    {
        auto [object, entry] = findObject(dictionary, 0x2000, 0);
        ASSERT_NE(object, nullptr);
        ASSERT_NE(entry,  nullptr);

        ASSERT_EQ(object->code, CoE::ObjectCode::RECORD);
        ASSERT_EQ(object->name, "Drive Parameters");
        ASSERT_EQ(object->entries.size(), 3);

        // SubIndex 0: USINT
        ASSERT_EQ(entry->type, CoE::DataType::UNSIGNED8);
        ASSERT_EQ(entry->bitlen, 8);
        ASSERT_NE(entry->data, nullptr);
        uint8_t sub0;
        std::memcpy(&sub0, entry->data, 1);
        ASSERT_EQ(sub0, 2);

        // SubIndex 1: DT_OperationMode -> USINT, DefaultValue=1
        auto [obj1, e1] = findObject(dictionary, 0x2000, 1);
        ASSERT_NE(e1, nullptr);
        ASSERT_EQ(e1->type, CoE::DataType::UNSIGNED8);
        ASSERT_EQ(e1->bitlen, 8);
        ASSERT_EQ(e1->bitoff, 8);
        ASSERT_NE(e1->data, nullptr);
        uint8_t mode;
        std::memcpy(&mode, e1->data, 1);
        ASSERT_EQ(mode, 1);

        // SubIndex 2: DT_Temperature -> INT, DefaultValue=#x0019 (25)
        auto [obj2, e2] = findObject(dictionary, 0x2000, 2);
        ASSERT_NE(e2, nullptr);
        ASSERT_EQ(e2->type, CoE::DataType::INTEGER16);
        ASSERT_EQ(e2->bitlen, 16);
        ASSERT_EQ(e2->bitoff, 16);
        ASSERT_NE(e2->data, nullptr);
        int16_t temp;
        std::memcpy(&temp, e2->data, 2);
        ASSERT_EQ(temp, 25);
    }

    // SM type object
    {
        auto [object, entry] = findObject(dictionary, 0x1C00, 0);
        ASSERT_NE(object, nullptr);
        ASSERT_EQ(object->code, CoE::ObjectCode::ARRAY);
        ASSERT_EQ(object->entries.size(), 5);
    }
}

TEST(EsiParser, load_basic_with_bit_types_and_no_info)
{
    CoE::EsiParser parser;
    auto dictionary = parser.loadFirstDictionaryFromFile("kickcat_esi_test_basic.xml");

    ASSERT_STREQ(parser.profile(), "5001");
    ASSERT_STREQ(parser.vendor(),  "KickCAT");

    // 8 objects in the dictionary + 1 SM type object generated by parser = 9
    ASSERT_EQ(dictionary.size(), 9);

    // 0x1000 - basic VAR with DefaultData
    {
        auto [object, entry] = findObject(dictionary, 0x1000, 0);
        ASSERT_NE(object, nullptr);
        ASSERT_NE(entry,  nullptr);

        ASSERT_EQ(object->code, CoE::ObjectCode::VAR);
        ASSERT_EQ(object->entries.size(), 1);
        ASSERT_EQ(entry->type, CoE::DataType::UNSIGNED32);
        ASSERT_EQ(entry->bitlen, 32);
        ASSERT_NE(entry->data, nullptr);
    }

    // 0x1008 - STRING type
    {
        auto [object, entry] = findObject(dictionary, 0x1008, 0);
        ASSERT_NE(object, nullptr);
        ASSERT_NE(entry,  nullptr);

        ASSERT_EQ(object->code, CoE::ObjectCode::VAR);
        ASSERT_EQ(entry->type, CoE::DataType::VISIBLE_STRING);
        ASSERT_EQ(entry->bitlen, 64);
    }

    // 0x10F8 - VAR without Info element (must not crash)
    {
        auto [object, entry] = findObject(dictionary, 0x10F8, 0);
        ASSERT_NE(object, nullptr);
        ASSERT_NE(entry,  nullptr);

        ASSERT_EQ(object->code, CoE::ObjectCode::VAR);
        ASSERT_EQ(object->name, "Timestamp Object");
        ASSERT_EQ(entry->type, CoE::DataType::UNSIGNED64);
        ASSERT_EQ(entry->bitlen, 64);
        ASSERT_EQ(entry->data, nullptr);
    }

    // 0x6000 - RECORD with BIT6 SubItem
    {
        auto [object, entry] = findObject(dictionary, 0x6000, 0);
        ASSERT_NE(object, nullptr);
        ASSERT_NE(entry,  nullptr);

        ASSERT_EQ(object->code, CoE::ObjectCode::RECORD);
        ASSERT_EQ(object->entries.size(), 5);

        // SubIndex 0: USINT
        auto [obj0, e0] = findObject(dictionary, 0x6000, 0);
        ASSERT_EQ(e0->type, CoE::DataType::UNSIGNED8);
        ASSERT_EQ(e0->bitlen, 8);

        // SubIndex 1: UINT (SensorValue)
        auto [obj1, e1] = findObject(dictionary, 0x6000, 1);
        ASSERT_NE(e1, nullptr);
        ASSERT_EQ(e1->type, CoE::DataType::UNSIGNED16);
        ASSERT_EQ(e1->bitlen, 16);
        ASSERT_EQ(e1->bitoff, 16);

        // SubIndex 2: BOOL (DigitalIn)
        auto [obj2, e2] = findObject(dictionary, 0x6000, 2);
        ASSERT_NE(e2, nullptr);
        ASSERT_EQ(e2->type, CoE::DataType::BOOLEAN);
        ASSERT_EQ(e2->bitlen, 1);
        ASSERT_EQ(e2->bitoff, 32);

        // SubIndex 3: BIT6 (Padding)
        auto [obj3, e3] = findObject(dictionary, 0x6000, 3);
        ASSERT_NE(e3, nullptr);
        ASSERT_EQ(e3->type, CoE::DataType::BIT6);
        ASSERT_EQ(e3->bitlen, 6);
        ASSERT_EQ(e3->bitoff, 33);

        // SubIndex 4: BOOL (StatusBit)
        auto [obj4, e4] = findObject(dictionary, 0x6000, 4);
        ASSERT_NE(e4, nullptr);
        ASSERT_EQ(e4->type, CoE::DataType::BOOLEAN);
        ASSERT_EQ(e4->bitlen, 1);
        ASSERT_EQ(e4->bitoff, 39);
    }

    // 0x7010 - RECORD with BIT6 SubItem (outputs)
    {
        auto [object, entry] = findObject(dictionary, 0x7010, 0);
        ASSERT_NE(object, nullptr);
        ASSERT_NE(entry,  nullptr);

        ASSERT_EQ(object->code, CoE::ObjectCode::RECORD);
        ASSERT_EQ(object->entries.size(), 5);

        auto [obj3, e3] = findObject(dictionary, 0x7010, 3);
        ASSERT_NE(e3, nullptr);
        ASSERT_EQ(e3->type, CoE::DataType::BIT6);
        ASSERT_EQ(e3->bitlen, 6);
    }

    // SM type object generated by parser (4 Sm elements)
    {
        auto [object, entry] = findObject(dictionary, 0x1C00, 0);
        ASSERT_NE(object, nullptr);
        ASSERT_NE(entry,  nullptr);

        ASSERT_EQ(object->code, CoE::ObjectCode::ARRAY);
        ASSERT_EQ(object->name, "Sync manager type");
        ASSERT_EQ(object->entries.size(), 5);
    }
}
