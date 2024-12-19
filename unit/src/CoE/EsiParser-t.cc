#include <gtest/gtest.h>
#include <algorithm>

#include "kickcat/CoE/EsiParser.h"

// This test is not a real unit test but an integration test: it could be rework later
// if the benefit/cost is OK

using namespace kickcat;

TEST(EsiParser, load_error_not_an_xml)
{
    CoE::EsiParser parser;
    ASSERT_THROW((void) parser.loadFile(""),   std::runtime_error);
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
    auto dictionary = parser.loadFile("foot.xml");

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
