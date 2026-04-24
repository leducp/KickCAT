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

TEST(EsiParser, load_complex_with_enums_and_default_value)
{
    CoE::EsiParser parser;
    auto dictionary = parser.loadFile("kickcat_esi_test_complex.xml");

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
    auto dictionary = parser.loadFile("kickcat_esi_test_basic.xml");

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
        ASSERT_EQ(e2->data_bit_offset, 0);
        ASSERT_NE(e2->data, nullptr);

        // SubIndex 3: BIT6 (Padding). Fixture's Info block has no "Padding"
        // SubItem, so per ETG2000 name-matching this entry gets no default.
        auto [obj3, e3] = findObject(dictionary, 0x6000, 3);
        ASSERT_NE(e3, nullptr);
        ASSERT_EQ(e3->type, CoE::DataType::BIT6);
        ASSERT_EQ(e3->bitlen, 6);
        ASSERT_EQ(e3->bitoff, 33);
        ASSERT_EQ(e3->data_bit_offset, 0);
        ASSERT_EQ(e3->data, nullptr);

        // SubIndex 4: BOOL (StatusBit). Fixture has "StatusBit" Info SubItem
        // with DefaultData, matched by name to this entry.
        auto [obj4, e4] = findObject(dictionary, 0x6000, 4);
        ASSERT_NE(e4, nullptr);
        ASSERT_EQ(e4->type, CoE::DataType::BOOLEAN);
        ASSERT_EQ(e4->bitlen, 1);
        ASSERT_EQ(e4->bitoff, 39);
        ASSERT_EQ(e4->data_bit_offset, 0);
        ASSERT_NE(e4->data, nullptr);
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

// ETG2000 4807: Info/SubItem is matched to DataType/SubItem by Name.
// Fixture reorders them and skips one to verify spec-conforming matching.
TEST(EsiParser, info_subitems_matched_by_name_not_position)
{
    std::string xml = R"(<?xml version="1.0"?>
<EtherCATInfo>
  <Vendor><Id>#x1</Id><Name>Test</Name></Vendor>
  <Descriptions>
    <Devices>
      <Device Physics="YY">
        <Type ProductCode="#x1" RevisionNo="#x1">ByName</Type>
        <Name>ByName</Name>
        <Profile>
          <ProfileNo>5001</ProfileNo>
          <Dictionary>
            <DataTypes>
              <DataType><Name>USINT</Name><BitSize>8</BitSize></DataType>
              <DataType><Name>UINT</Name><BitSize>16</BitSize></DataType>
              <DataType>
                <Name>DT6000</Name><BitSize>24</BitSize>
                <SubItem><SubIdx>0</SubIdx><Name>SubIndex 000</Name><Type>USINT</Type><BitSize>8</BitSize><BitOffs>0</BitOffs></SubItem>
                <SubItem><SubIdx>1</SubIdx><Name>Alpha</Name><Type>UINT</Type><BitSize>16</BitSize><BitOffs>8</BitOffs></SubItem>
                <SubItem><SubIdx>2</SubIdx><Name>Bravo</Name><Type>UINT</Type><BitSize>16</BitSize><BitOffs>24</BitOffs></SubItem>
                <SubItem><SubIdx>3</SubIdx><Name>Charlie</Name><Type>UINT</Type><BitSize>16</BitSize><BitOffs>40</BitOffs></SubItem>
              </DataType>
            </DataTypes>
            <Objects>
              <Object>
                <Index>#x6000</Index><Name>Data</Name><Type>DT6000</Type><BitSize>56</BitSize>
                <Info>
                  <!-- Order intentionally reshuffled; 'Bravo' is omitted. -->
                  <SubItem><Name>Charlie</Name><Info><DefaultData>CCCC</DefaultData></Info></SubItem>
                  <SubItem><Name>SubIndex 000</Name><Info><DefaultData>03</DefaultData></Info></SubItem>
                  <SubItem><Name>Alpha</Name><Info><DefaultData>AAAA</DefaultData></Info></SubItem>
                </Info>
              </Object>
            </Objects>
          </Dictionary>
        </Profile>
        <Sm StartAddress="#x1000" ControlByte="#x26" Enable="1">MBoxOut</Sm>
      </Device>
    </Devices>
  </Descriptions>
</EtherCATInfo>
)";

    CoE::EsiParser parser;
    auto dict = parser.loadString(xml);

    auto [obj0, si0] = CoE::findObject(dict, 0x6000, 0);
    auto [obj1, si1] = CoE::findObject(dict, 0x6000, 1);
    auto [obj2, si2] = CoE::findObject(dict, 0x6000, 2);
    auto [obj3, si3] = CoE::findObject(dict, 0x6000, 3);
    ASSERT_NE(si0, nullptr);
    ASSERT_NE(si1, nullptr);
    ASSERT_NE(si2, nullptr);
    ASSERT_NE(si3, nullptr);

    ASSERT_NE(si0->data, nullptr);
    EXPECT_EQ(*static_cast<uint8_t*>(si0->data), 0x03);

    ASSERT_NE(si1->data, nullptr);
    EXPECT_EQ(*static_cast<uint16_t*>(si1->data), 0xAAAA);

    EXPECT_EQ(si2->data, nullptr);

    ASSERT_NE(si3->data, nullptr);
    EXPECT_EQ(*static_cast<uint16_t*>(si3->data), 0xCCCC);
}
