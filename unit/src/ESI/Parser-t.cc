#include <gtest/gtest.h>
#include <algorithm>

#include "kickcat/ESI/Parser.h"
#include "kickcat/CoE/EsiParser.h"

// This test is not a real unit test but an integration test: it could be reworked later
// if the benefit/cost is OK.

using namespace kickcat;

TEST(ESIParser, load_error_not_an_xml)
{
    ESI::Parser parser;
    ASSERT_THROW((void) parser.loadFile(""),   std::runtime_error);
    ASSERT_THROW((void) parser.loadString(""), std::runtime_error);
}

TEST(ESIParser, load_error_xml_not_an_esi)
{
    ESI::Parser parser;
    ASSERT_THROW((void) parser.loadString("<test></test>"), std::invalid_argument);
}


TEST(ESIParser, load)
{
    ESI::Parser parser;
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

TEST(ESIParser, load_complex_with_enums_and_default_value)
{
    ESI::Parser parser;
    auto dictionary = parser.loadFile("kickcat_esi_test_complex.xml");

    ASSERT_STREQ(parser.profile(), "5002");
    ASSERT_STREQ(parser.vendor(),  "KickCAT");

    // 5 objects + 1 SM type object = 6
    ASSERT_EQ(dictionary.size(), 6);

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

    {
        auto [object, entry] = findObject(dictionary, 0x2000, 0);
        ASSERT_NE(object, nullptr);
        ASSERT_NE(entry,  nullptr);

        ASSERT_EQ(object->code, CoE::ObjectCode::RECORD);
        ASSERT_EQ(object->name, "Drive Parameters");
        ASSERT_EQ(object->entries.size(), 3);

        ASSERT_EQ(entry->type, CoE::DataType::UNSIGNED8);
        ASSERT_EQ(entry->bitlen, 8);
        ASSERT_NE(entry->data, nullptr);
        uint8_t sub0;
        std::memcpy(&sub0, entry->data, 1);
        ASSERT_EQ(sub0, 2);

        auto [obj1, e1] = findObject(dictionary, 0x2000, 1);
        ASSERT_NE(e1, nullptr);
        ASSERT_EQ(e1->type, CoE::DataType::UNSIGNED8);
        ASSERT_EQ(e1->bitlen, 8);
        ASSERT_EQ(e1->bitoff, 8);
        ASSERT_NE(e1->data, nullptr);
        uint8_t mode;
        std::memcpy(&mode, e1->data, 1);
        ASSERT_EQ(mode, 1);

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

    {
        auto [object, entry] = findObject(dictionary, 0x1C00, 0);
        ASSERT_NE(object, nullptr);
        ASSERT_EQ(object->code, CoE::ObjectCode::ARRAY);
        ASSERT_EQ(object->entries.size(), 5);
    }
}

TEST(ESIParser, load_basic_with_bit_types_and_no_info)
{
    ESI::Parser parser;
    auto dictionary = parser.loadFile("kickcat_esi_test_basic.xml");

    ASSERT_STREQ(parser.profile(), "5001");
    ASSERT_STREQ(parser.vendor(),  "KickCAT");

    // 8 objects + 1 SM type object = 9
    ASSERT_EQ(dictionary.size(), 9);

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

    {
        auto [object, entry] = findObject(dictionary, 0x1008, 0);
        ASSERT_NE(object, nullptr);
        ASSERT_NE(entry,  nullptr);

        ASSERT_EQ(object->code, CoE::ObjectCode::VAR);
        ASSERT_EQ(entry->type, CoE::DataType::VISIBLE_STRING);
        ASSERT_EQ(entry->bitlen, 64);
    }

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

    {
        auto [object, entry] = findObject(dictionary, 0x6000, 0);
        ASSERT_NE(object, nullptr);
        ASSERT_NE(entry,  nullptr);

        ASSERT_EQ(object->code, CoE::ObjectCode::RECORD);
        ASSERT_EQ(object->entries.size(), 5);

        auto [obj0, e0] = findObject(dictionary, 0x6000, 0);
        ASSERT_EQ(e0->type, CoE::DataType::UNSIGNED8);
        ASSERT_EQ(e0->bitlen, 8);

        auto [obj1, e1] = findObject(dictionary, 0x6000, 1);
        ASSERT_NE(e1, nullptr);
        ASSERT_EQ(e1->type, CoE::DataType::UNSIGNED16);
        ASSERT_EQ(e1->bitlen, 16);
        ASSERT_EQ(e1->bitoff, 16);

        auto [obj2, e2] = findObject(dictionary, 0x6000, 2);
        ASSERT_NE(e2, nullptr);
        ASSERT_EQ(e2->type, CoE::DataType::BOOLEAN);
        ASSERT_EQ(e2->bitlen, 1);
        ASSERT_EQ(e2->bitoff, 32);

        auto [obj3, e3] = findObject(dictionary, 0x6000, 3);
        ASSERT_NE(e3, nullptr);
        ASSERT_EQ(e3->type, CoE::DataType::BIT6);
        ASSERT_EQ(e3->bitlen, 6);
        ASSERT_EQ(e3->bitoff, 33);

        auto [obj4, e4] = findObject(dictionary, 0x6000, 4);
        ASSERT_NE(e4, nullptr);
        ASSERT_EQ(e4->type, CoE::DataType::BOOLEAN);
        ASSERT_EQ(e4->bitlen, 1);
        ASSERT_EQ(e4->bitoff, 39);
    }

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

    {
        auto [object, entry] = findObject(dictionary, 0x1C00, 0);
        ASSERT_NE(object, nullptr);
        ASSERT_NE(entry,  nullptr);

        ASSERT_EQ(object->code, CoE::ObjectCode::ARRAY);
        ASSERT_EQ(object->name, "Sync manager type");
        ASSERT_EQ(object->entries.size(), 5);
    }
}

TEST(ESIParser, loadDevice_populates_aggregate)
{
    ESI::Parser parser;
    ESI::Device device = parser.loadDevice("kickcat_esi_test_basic.xml");

    ASSERT_EQ(device.vendor_name, "KickCAT");
    ASSERT_EQ(device.vendor_id,   0x0CAFE);
    ASSERT_EQ(device.type,         "kickcat_esi_test_basic");
    ASSERT_EQ(device.product_code, 0x00001234u);
    ASSERT_EQ(device.revision_no,  0x1u);
    ASSERT_EQ(device.name,         "KickCAT ESI Test Basic");
    ASSERT_EQ(device.group_type,   "KickCAT Test Devices");
    ASSERT_EQ(device.profile_no,   5001u);
    ASSERT_EQ(device.dictionary.size(), 9u);
}

TEST(ESIParser, listDevices_enumerates_all)
{
    ESI::Parser parser;
    auto summaries = parser.listDevices("kickcat_esi_test_multi_device.xml");

    ASSERT_EQ(summaries.size(), 3u);

    ASSERT_EQ(summaries[0].type,         "kickcat_multi_dev_alpha");
    ASSERT_EQ(summaries[0].product_code, 0x00001111u);
    ASSERT_EQ(summaries[0].revision_no,  0x1u);

    ASSERT_EQ(summaries[1].type,         "kickcat_multi_dev_beta");
    ASSERT_EQ(summaries[1].product_code, 0x00002222u);
    ASSERT_EQ(summaries[1].revision_no,  0x1u);

    ASSERT_EQ(summaries[2].type,         "kickcat_multi_dev_beta");
    ASSERT_EQ(summaries[2].product_code, 0x00002222u);
    ASSERT_EQ(summaries[2].revision_no,  0x2u);
}

TEST(ESIParser, loadDevice_default_picks_first)
{
    ESI::Parser parser;
    ESI::Device device = parser.loadDevice("kickcat_esi_test_multi_device.xml");

    ASSERT_EQ(device.type,         "kickcat_multi_dev_alpha");
    ASSERT_EQ(device.product_code, 0x00001111u);
    ASSERT_EQ(device.profile_no,   5101u);
}

TEST(ESIParser, loadDevice_filter_by_index)
{
    ESI::Parser parser;
    ESI::DeviceFilter filter;
    filter.index = 2;
    ESI::Device device = parser.loadDevice("kickcat_esi_test_multi_device.xml", filter);

    ASSERT_EQ(device.product_code, 0x00002222u);
    ASSERT_EQ(device.revision_no,  0x2u);
    ASSERT_EQ(device.profile_no,   5103u);
}

TEST(ESIParser, loadDevice_filter_by_product_and_revision)
{
    ESI::Parser parser;
    ESI::DeviceFilter filter;
    filter.product_code = 0x00002222u;
    filter.revision_no  = 0x2u;
    ESI::Device device = parser.loadDevice("kickcat_esi_test_multi_device.xml", filter);

    ASSERT_EQ(device.type,         "kickcat_multi_dev_beta");
    ASSERT_EQ(device.product_code, 0x00002222u);
    ASSERT_EQ(device.revision_no,  0x2u);
    ASSERT_EQ(device.profile_no,   5103u);
}

TEST(ESIParser, loadDevice_filter_by_type)
{
    ESI::Parser parser;
    ESI::DeviceFilter filter;
    filter.type = "kickcat_multi_dev_beta";
    ESI::Device device = parser.loadDevice("kickcat_esi_test_multi_device.xml", filter);

    // first match wins (revision 1)
    ASSERT_EQ(device.product_code, 0x00002222u);
    ASSERT_EQ(device.revision_no,  0x1u);
}

TEST(ESIParser, loadDevice_filter_no_match_throws)
{
    ESI::Parser parser;
    ESI::DeviceFilter filter;
    filter.product_code = 0xDEADBEEFu;
    ASSERT_THROW((void) parser.loadDevice("kickcat_esi_test_multi_device.xml", filter),
                 std::invalid_argument);
}

TEST(ESIParser, CoE_alias_is_backwards_compatible)
{
    // The CoE::EsiParser alias must still resolve to ESI::Parser.
    CoE::EsiParser parser;
    auto dictionary = parser.loadFile("kickcat_esi_test_basic.xml");
    ASSERT_EQ(dictionary.size(), 9u);
    ASSERT_STREQ(parser.vendor(), "KickCAT");
}
