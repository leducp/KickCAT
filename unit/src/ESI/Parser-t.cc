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

TEST(ESIParser, throws_with_context_when_object_missing_name)
{
    char const* xml = R"(<?xml version="1.0"?>
        <EtherCATInfo>
            <Vendor><Id>#x1</Id><Name>V</Name></Vendor>
            <Descriptions><Devices><Device>
                <Type ProductCode="#x1" RevisionNo="#x1">T</Type>
                <Profile><ProfileNo>0</ProfileNo>
                    <Dictionary>
                        <DataTypes><DataType><Name>UDINT</Name><BitSize>32</BitSize></DataType></DataTypes>
                        <Objects>
                            <Object>
                                <Index>#x1000</Index>
                                <Type>UDINT</Type>
                                <BitSize>32</BitSize>
                            </Object>
                        </Objects>
                    </Dictionary>
                </Profile>
            </Device></Devices></Descriptions>
        </EtherCATInfo>)";

    ESI::Parser parser;
    try
    {
        (void) parser.loadString(xml);
        FAIL() << "expected invalid_argument";
    }
    catch (std::invalid_argument const& e)
    {
        std::string msg = e.what();
        ASSERT_NE(msg.find("Name"),         std::string::npos) << msg;
        ASSERT_NE(msg.find("Object 0x1000"), std::string::npos) << msg;
    }
}

TEST(ESIParser, throws_with_context_when_object_missing_index)
{
    char const* xml = R"(<?xml version="1.0"?>
        <EtherCATInfo>
            <Vendor><Id>#x1</Id><Name>V</Name></Vendor>
            <Descriptions><Devices><Device>
                <Type ProductCode="#x1" RevisionNo="#x1">T</Type>
                <Profile><ProfileNo>0</ProfileNo>
                    <Dictionary>
                        <DataTypes><DataType><Name>UDINT</Name><BitSize>32</BitSize></DataType></DataTypes>
                        <Objects>
                            <Object>
                                <Name>NoIndex</Name>
                                <Type>UDINT</Type>
                                <BitSize>32</BitSize>
                            </Object>
                        </Objects>
                    </Dictionary>
                </Profile>
            </Device></Devices></Descriptions>
        </EtherCATInfo>)";

    ESI::Parser parser;
    ASSERT_THROW((void) parser.loadString(xml), std::invalid_argument);
}

TEST(ESIParser, throws_on_basetype_cycle)
{
    char const* xml = R"(<?xml version="1.0"?>
        <EtherCATInfo>
            <Vendor><Id>#x1</Id><Name>V</Name></Vendor>
            <Descriptions><Devices><Device>
                <Type ProductCode="#x1" RevisionNo="#x1">T</Type>
                <Profile><ProfileNo>0</ProfileNo>
                    <Dictionary>
                        <DataTypes>
                            <DataType><Name>DT_A</Name><BaseType>DT_B</BaseType><BitSize>32</BitSize></DataType>
                            <DataType><Name>DT_B</Name><BaseType>DT_A</BaseType><BitSize>32</BitSize></DataType>
                        </DataTypes>
                        <Objects>
                            <Object>
                                <Index>#x1000</Index>
                                <Name>Cycle</Name>
                                <Type>DT_A</Type>
                                <BitSize>32</BitSize>
                            </Object>
                        </Objects>
                    </Dictionary>
                </Profile>
            </Device></Devices></Descriptions>
        </EtherCATInfo>)";

    ESI::Parser parser;
    try
    {
        (void) parser.loadString(xml);
        FAIL() << "expected invalid_argument";
    }
    catch (std::invalid_argument const& e)
    {
        std::string msg = e.what();
        ASSERT_NE(msg.find("recursion"), std::string::npos) << msg;
    }
}

TEST(ESIParser, throws_when_subitem_type_unresolved)
{
    // RECORD references DT_Missing which is not in <DataTypes>.
    char const* xml = R"(<?xml version="1.0"?>
        <EtherCATInfo>
            <Vendor><Id>#x1</Id><Name>V</Name></Vendor>
            <Descriptions><Devices><Device>
                <Type ProductCode="#x1" RevisionNo="#x1">T</Type>
                <Profile><ProfileNo>0</ProfileNo>
                    <Dictionary>
                        <DataTypes>
                            <DataType><Name>USINT</Name><BitSize>8</BitSize></DataType>
                        </DataTypes>
                        <Objects>
                            <Object>
                                <Index>#x2000</Index>
                                <Name>Bad</Name>
                                <Type>DT_Missing</Type>
                                <BitSize>16</BitSize>
                            </Object>
                        </Objects>
                    </Dictionary>
                </Profile>
            </Device></Devices></Descriptions>
        </EtherCATInfo>)";

    ESI::Parser parser;
    try
    {
        (void) parser.loadString(xml);
        FAIL() << "expected invalid_argument";
    }
    catch (std::invalid_argument const& e)
    {
        std::string msg = e.what();
        ASSERT_NE(msg.find("Object 0x2000"), std::string::npos) << msg;
    }
}

TEST(ESIParser, loadDevice_parses_sync_managers_with_attributes)
{
    ESI::Parser parser;
    ESI::Device device = parser.loadDevice("kickcat_esi_test_sm_fmmu.xml");

    ASSERT_EQ(device.sync_managers.size(), 4u);

    auto const& mbox_out = device.sync_managers[0];
    ASSERT_EQ(mbox_out.type,          SyncManager::MailboxOut);
    ASSERT_EQ(mbox_out.min_size,      40);
    ASSERT_EQ(mbox_out.max_size,      1486);
    ASSERT_EQ(mbox_out.default_size,  128);
    ASSERT_EQ(mbox_out.start_address, 0x1000);
    ASSERT_EQ(mbox_out.control_byte,  0x26);
    ASSERT_EQ(mbox_out.enable,        1);
    ASSERT_FALSE(mbox_out.is_virtual);
    ASSERT_FALSE(mbox_out.op_only);

    auto const& mbox_in = device.sync_managers[1];
    ASSERT_EQ(mbox_in.type,          SyncManager::MailboxIn);
    ASSERT_EQ(mbox_in.start_address, 0x1400);
    ASSERT_EQ(mbox_in.control_byte,  0x22);
    ASSERT_TRUE(mbox_in.is_virtual);     // Virtual="1"
    ASSERT_FALSE(mbox_in.op_only);

    auto const& outputs = device.sync_managers[2];
    ASSERT_EQ(outputs.type,          SyncManager::Output);
    ASSERT_EQ(outputs.min_size,      0);  // attribute absent
    ASSERT_EQ(outputs.default_size,  40);
    ASSERT_EQ(outputs.start_address, 0x1800);
    ASSERT_EQ(outputs.control_byte,  0x64);
    ASSERT_TRUE(outputs.op_only);        // OpOnly="true"

    auto const& inputs = device.sync_managers[3];
    ASSERT_EQ(inputs.type,          SyncManager::Input);
    ASSERT_EQ(inputs.default_size,  22);
    ASSERT_EQ(inputs.start_address, 0x1c00);
}

TEST(ESIParser, loadDevice_parses_fmmus)
{
    ESI::Parser parser;
    ESI::Device device = parser.loadDevice("kickcat_esi_test_sm_fmmu.xml");

    ASSERT_EQ(device.fmmus.size(), 3u);

    ASSERT_EQ(device.fmmus[0].type, fmmu::Outputs);
    ASSERT_EQ(device.fmmus[0].sm,   2);
    ASSERT_TRUE(device.fmmus[0].op_only);

    ASSERT_EQ(device.fmmus[1].type, fmmu::Inputs);
    ASSERT_EQ(device.fmmus[1].sm,   3);
    ASSERT_FALSE(device.fmmus[1].op_only);

    ASSERT_EQ(device.fmmus[2].type, fmmu::MBoxState);
    ASSERT_EQ(device.fmmus[2].sm,   -1);    // absent
    ASSERT_EQ(device.fmmus[2].su,   -1);
}

TEST(ESIParser, loadDevice_parses_sync_units)
{
    ESI::Parser parser;
    ESI::Device device = parser.loadDevice("kickcat_esi_test_sm_fmmu.xml");

    ASSERT_EQ(device.sync_units.size(), 1u);
    ASSERT_TRUE (device.sync_units[0].separate_su);             // "true"
    ASSERT_FALSE(device.sync_units[0].separate_frame);          // "0"
    ASSERT_TRUE (device.sync_units[0].frame_repeat_support);    // "1"
}

TEST(ESIParser, sync_managers_drive_legacy_0x1C00_synthesis)
{
    // The synthesized 0x1C00 array must reflect the parsed SM list.
    ESI::Parser parser;
    auto dictionary = parser.loadFile("kickcat_esi_test_sm_fmmu.xml");

    auto [object, entry] = findObject(dictionary, 0x1C00, 0);
    ASSERT_NE(object, nullptr);
    ASSERT_EQ(object->code, CoE::ObjectCode::ARRAY);
    ASSERT_EQ(object->entries.size(), 5u);  // 1 size byte + 4 SMs

    uint8_t size = 0;
    std::memcpy(&size, object->entries[0].data, 1);
    ASSERT_EQ(size, 4u);

    uint8_t sm_kinds[4];
    for (int i = 0; i < 4; ++i)
    {
        std::memcpy(&sm_kinds[i], object->entries[i + 1].data, 1);
    }
    ASSERT_EQ(sm_kinds[0], 1u);   // MBoxOut
    ASSERT_EQ(sm_kinds[1], 2u);   // MBoxIn
    ASSERT_EQ(sm_kinds[2], 3u);   // Outputs
    ASSERT_EQ(sm_kinds[3], 4u);   // Inputs
}

TEST(ESIParser, loadDevice_parses_mailbox)
{
    ESI::Parser parser;
    ESI::Device device = parser.loadDevice("kickcat_esi_test_sm_fmmu.xml");

    ASSERT_TRUE(device.mailbox.has_value());
    auto const& mailbox = *device.mailbox;
    ASSERT_TRUE (mailbox.data_link_layer);
    ASSERT_FALSE(mailbox.real_time_mode);

    ASSERT_TRUE(mailbox.coe.has_value());
    auto const& coe = *mailbox.coe;
    ASSERT_TRUE(coe.sdo_info);
    ASSERT_TRUE(coe.complete_access);
    ASSERT_TRUE(coe.pdo_assign);
    ASSERT_TRUE(coe.pdo_config);
    ASSERT_TRUE(coe.segmented_sdo);
    ASSERT_FALSE(coe.diag_history);
    ASSERT_EQ(coe.eds_file, "MyDevice.eds");
    ASSERT_EQ(coe.init_cmds.size(), 2u);

    auto const& coe_ic0 = coe.init_cmds[0];
    ASSERT_EQ(coe_ic0.transitions.size(), 1u);
    ASSERT_EQ(coe_ic0.transitions[0], ESI::transition::PS);
    ASSERT_EQ(coe_ic0.index,    0x1C12);
    ASSERT_EQ(coe_ic0.subindex, 0x00);
    ASSERT_EQ(coe_ic0.data.size(), 2u);
    ASSERT_EQ(coe_ic0.data[0], 0x00);
    ASSERT_EQ(coe_ic0.data[1], 0x01);
    ASSERT_TRUE(coe_ic0.adapt_automatically);
    ASSERT_TRUE(coe_ic0.complete_access);
    ASSERT_EQ(coe_ic0.comment, "Assign RxPDO 0x1600");

    ASSERT_TRUE(mailbox.eoe.has_value());
    auto const& eoe = *mailbox.eoe;
    ASSERT_TRUE(eoe.ip);
    ASSERT_TRUE(eoe.mac);
    ASSERT_FALSE(eoe.time_stamp);
    ASSERT_EQ(eoe.init_cmds.size(), 1u);
    ASSERT_EQ(eoe.init_cmds[0].transitions.size(), 2u);
    ASSERT_EQ(eoe.init_cmds[0].transitions[0], ESI::transition::IP);
    ASSERT_EQ(eoe.init_cmds[0].transitions[1], ESI::transition::PS);
    ASSERT_EQ(eoe.init_cmds[0].type, 5);

    ASSERT_TRUE(mailbox.aoe.has_value());
    auto const& aoe = *mailbox.aoe;
    ASSERT_TRUE(aoe.ads_router);
    ASSERT_TRUE(aoe.generate_own_net_id);
    ASSERT_FALSE(aoe.initialize_own_net_id);
    ASSERT_EQ(aoe.init_cmds.size(), 1u);
    ASSERT_EQ(aoe.init_cmds[0].comment, "AoE init");
    ASSERT_EQ(aoe.init_cmds[0].data.size(), 4u);

    ASSERT_TRUE(mailbox.soe.has_value());
    auto const& soe = *mailbox.soe;
    ASSERT_TRUE(soe.channel_count.has_value());
    ASSERT_EQ(*soe.channel_count, 2);
    ASSERT_TRUE(soe.drive_follows_bit3);
    ASSERT_EQ(soe.init_cmds.size(), 1u);
    ASSERT_EQ(soe.init_cmds[0].idn, 32);
    ASSERT_EQ(soe.init_cmds[0].channel, 1);

    ASSERT_TRUE(mailbox.foe.has_value());
    ASSERT_TRUE(mailbox.voe.has_value());
}

TEST(ESIParser, mailbox_absent_when_block_missing)
{
    ESI::Parser parser;
    ESI::Device device = parser.loadDevice("kickcat_esi_test_multi_device.xml");

    ASSERT_FALSE(device.mailbox.has_value());
}

TEST(ESIParser, mailbox_throws_on_unknown_transition)
{
    char const* xml = R"(<?xml version="1.0"?>
        <EtherCATInfo>
            <Vendor><Id>#x1</Id><Name>V</Name></Vendor>
            <Descriptions><Devices><Device>
                <Type ProductCode="#x1" RevisionNo="#x1">T</Type>
                <Profile><ProfileNo>0</ProfileNo>
                    <Dictionary>
                        <DataTypes><DataType><Name>UDINT</Name><BitSize>32</BitSize></DataType></DataTypes>
                        <Objects>
                            <Object><Index>#x1000</Index><Name>X</Name><Type>UDINT</Type><BitSize>32</BitSize></Object>
                        </Objects>
                    </Dictionary>
                </Profile>
                <Mailbox>
                    <CoE>
                        <InitCmd>
                            <Transition>XX</Transition>
                            <Index>#x1000</Index>
                            <SubIndex>#x00</SubIndex>
                            <Data>00</Data>
                        </InitCmd>
                    </CoE>
                </Mailbox>
            </Device></Devices></Descriptions>
        </EtherCATInfo>)";

    ESI::Parser parser;
    try
    {
        (void) parser.loadString(xml);
        FAIL() << "expected invalid_argument";
    }
    catch (std::invalid_argument const& e)
    {
        std::string msg = e.what();
        ASSERT_NE(msg.find("Transition"), std::string::npos) << msg;
        ASSERT_NE(msg.find("XX"),         std::string::npos) << msg;
    }
}

TEST(ESIParser, mailbox_throws_when_coe_initcmd_missing_data)
{
    char const* xml = R"(<?xml version="1.0"?>
        <EtherCATInfo>
            <Vendor><Id>#x1</Id><Name>V</Name></Vendor>
            <Descriptions><Devices><Device>
                <Type ProductCode="#x1" RevisionNo="#x1">T</Type>
                <Profile><ProfileNo>0</ProfileNo>
                    <Dictionary>
                        <DataTypes><DataType><Name>UDINT</Name><BitSize>32</BitSize></DataType></DataTypes>
                        <Objects>
                            <Object><Index>#x1000</Index><Name>X</Name><Type>UDINT</Type><BitSize>32</BitSize></Object>
                        </Objects>
                    </Dictionary>
                </Profile>
                <Mailbox>
                    <CoE>
                        <InitCmd>
                            <Transition>PS</Transition>
                            <Index>#x1000</Index>
                            <SubIndex>#x00</SubIndex>
                        </InitCmd>
                    </CoE>
                </Mailbox>
            </Device></Devices></Descriptions>
        </EtherCATInfo>)";

    ESI::Parser parser;
    ASSERT_THROW((void) parser.loadString(xml), std::invalid_argument);
}

TEST(ESIParser, mailbox_throws_when_initcmd_has_no_transition)
{
    char const* xml = R"(<?xml version="1.0"?>
        <EtherCATInfo>
            <Vendor><Id>#x1</Id><Name>V</Name></Vendor>
            <Descriptions><Devices><Device>
                <Type ProductCode="#x1" RevisionNo="#x1">T</Type>
                <Profile><ProfileNo>0</ProfileNo>
                    <Dictionary>
                        <DataTypes><DataType><Name>UDINT</Name><BitSize>32</BitSize></DataType></DataTypes>
                        <Objects>
                            <Object><Index>#x1000</Index><Name>X</Name><Type>UDINT</Type><BitSize>32</BitSize></Object>
                        </Objects>
                    </Dictionary>
                </Profile>
                <Mailbox>
                    <CoE>
                        <InitCmd>
                            <Index>#x1000</Index>
                            <SubIndex>#x00</SubIndex>
                            <Data>00</Data>
                        </InitCmd>
                    </CoE>
                </Mailbox>
            </Device></Devices></Descriptions>
        </EtherCATInfo>)";

    ESI::Parser parser;
    try
    {
        (void) parser.loadString(xml);
        FAIL() << "expected invalid_argument";
    }
    catch (std::invalid_argument const& e)
    {
        std::string msg = e.what();
        ASSERT_NE(msg.find("Transition"), std::string::npos) << msg;
    }
}

TEST(ESIParser, throws_on_malformed_bool_attribute)
{
    char const* xml = R"(<?xml version="1.0"?>
        <EtherCATInfo>
            <Vendor><Id>#x1</Id><Name>V</Name></Vendor>
            <Descriptions><Devices><Device>
                <Type ProductCode="#x1" RevisionNo="#x1">T</Type>
                <Profile><ProfileNo>0</ProfileNo>
                    <Dictionary>
                        <DataTypes><DataType><Name>UDINT</Name><BitSize>32</BitSize></DataType></DataTypes>
                        <Objects>
                            <Object><Index>#x1000</Index><Name>X</Name><Type>UDINT</Type><BitSize>32</BitSize></Object>
                        </Objects>
                    </Dictionary>
                </Profile>
                <Sm Virtual="yes" StartAddress="#x1000" ControlByte="#x26" Enable="1">MBoxOut</Sm>
            </Device></Devices></Descriptions>
        </EtherCATInfo>)";

    ESI::Parser parser;
    try
    {
        (void) parser.loadString(xml);
        FAIL() << "expected invalid_argument";
    }
    catch (std::invalid_argument const& e)
    {
        std::string msg = e.what();
        ASSERT_NE(msg.find("Virtual"), std::string::npos) << msg;
        ASSERT_NE(msg.find("yes"),     std::string::npos) << msg;
    }
}

TEST(ESIParser, accepts_canonical_bool_values_false_and_zero)
{
    // "false" and "0" are canonical xs:boolean values; ensure they don't throw.
    char const* xml = R"(<?xml version="1.0"?>
        <EtherCATInfo>
            <Vendor><Id>#x1</Id><Name>V</Name></Vendor>
            <Descriptions><Devices><Device>
                <Type ProductCode="#x1" RevisionNo="#x1">T</Type>
                <Profile><ProfileNo>0</ProfileNo>
                    <Dictionary>
                        <DataTypes><DataType><Name>UDINT</Name><BitSize>32</BitSize></DataType></DataTypes>
                        <Objects>
                            <Object><Index>#x1000</Index><Name>X</Name><Type>UDINT</Type><BitSize>32</BitSize></Object>
                        </Objects>
                    </Dictionary>
                </Profile>
                <Sm Virtual="false" OpOnly="0" StartAddress="#x1000" ControlByte="#x26" Enable="1">MBoxOut</Sm>
            </Device></Devices></Descriptions>
        </EtherCATInfo>)";

    ESI::Parser parser;
    ESI::Device device = parser.loadDeviceString(xml);
    ASSERT_EQ(device.sync_managers.size(), 1u);
    ASSERT_FALSE(device.sync_managers[0].is_virtual);
    ASSERT_FALSE(device.sync_managers[0].op_only);
}

TEST(ESIParser, mailbox_soe_initcmd_throws_on_malformed_chn)
{
    char const* xml = R"(<?xml version="1.0"?>
        <EtherCATInfo>
            <Vendor><Id>#x1</Id><Name>V</Name></Vendor>
            <Descriptions><Devices><Device>
                <Type ProductCode="#x1" RevisionNo="#x1">T</Type>
                <Profile><ProfileNo>0</ProfileNo>
                    <Dictionary>
                        <DataTypes><DataType><Name>UDINT</Name><BitSize>32</BitSize></DataType></DataTypes>
                        <Objects>
                            <Object><Index>#x1000</Index><Name>X</Name><Type>UDINT</Type><BitSize>32</BitSize></Object>
                        </Objects>
                    </Dictionary>
                </Profile>
                <Mailbox>
                    <SoE>
                        <InitCmd Chn="abc">
                            <Transition>PS</Transition>
                            <IDN>5</IDN>
                            <Data>00</Data>
                        </InitCmd>
                    </SoE>
                </Mailbox>
            </Device></Devices></Descriptions>
        </EtherCATInfo>)";

    ESI::Parser parser;
    try
    {
        (void) parser.loadString(xml);
        FAIL() << "expected invalid_argument";
    }
    catch (std::invalid_argument const& e)
    {
        std::string msg = e.what();
        ASSERT_NE(msg.find("Chn"), std::string::npos) << msg;
    }
}

TEST(ESIParser, mailbox_soe_initcmd_channel_defaults_to_zero)
{
    char const* xml = R"(<?xml version="1.0"?>
        <EtherCATInfo>
            <Vendor><Id>#x1</Id><Name>V</Name></Vendor>
            <Descriptions><Devices><Device>
                <Type ProductCode="#x1" RevisionNo="#x1">T</Type>
                <Profile><ProfileNo>0</ProfileNo>
                    <Dictionary>
                        <DataTypes><DataType><Name>UDINT</Name><BitSize>32</BitSize></DataType></DataTypes>
                        <Objects>
                            <Object><Index>#x1000</Index><Name>X</Name><Type>UDINT</Type><BitSize>32</BitSize></Object>
                        </Objects>
                    </Dictionary>
                </Profile>
                <Mailbox>
                    <SoE>
                        <InitCmd>
                            <Transition>PS</Transition>
                            <IDN>5</IDN>
                            <Data>00</Data>
                        </InitCmd>
                    </SoE>
                </Mailbox>
            </Device></Devices></Descriptions>
        </EtherCATInfo>)";

    ESI::Parser parser;
    ESI::Device device = parser.loadDeviceString(xml);
    ASSERT_TRUE(device.mailbox.has_value());
    ASSERT_TRUE(device.mailbox->soe.has_value());
    ASSERT_EQ(device.mailbox->soe->init_cmds.size(), 1u);
    ASSERT_EQ(device.mailbox->soe->init_cmds[0].channel, 0);
}

TEST(ESIParser, loadDevice_parses_pdos)
{
    ESI::Parser parser;
    ESI::Device device = parser.loadDevice("kickcat_esi_test_sm_fmmu.xml");

    ASSERT_EQ(device.rx_pdos.size(), 1u);
    auto const& rx = device.rx_pdos[0];
    ASSERT_EQ(rx.index, 0x1600u);
    ASSERT_EQ(rx.name,  "Outputs");
    ASSERT_TRUE(rx.sm.has_value());
    ASSERT_EQ(*rx.sm,    2);
    ASSERT_TRUE(rx.pdo_order.has_value());
    ASSERT_EQ(*rx.pdo_order, 5);
    ASSERT_TRUE(rx.mandatory);
    ASSERT_TRUE(rx.fixed);
    ASSERT_EQ(rx.exclude.size(), 1u);
    ASSERT_EQ(rx.exclude[0], 0x1601u);
    ASSERT_EQ(rx.entries.size(), 1u);
    ASSERT_EQ(rx.entries[0].index,    0x7000u);
    ASSERT_EQ(rx.entries[0].subindex, 1u);
    ASSERT_EQ(rx.entries[0].bit_len,  16u);
    ASSERT_EQ(rx.entries[0].name,     "OutputWord");
    ASSERT_EQ(rx.entries[0].data_type, "UINT");

    ASSERT_EQ(device.tx_pdos.size(), 1u);
    auto const& tx = device.tx_pdos[0];
    ASSERT_EQ(tx.index, 0x1A00u);
    ASSERT_TRUE(tx.os_fac.has_value());
    ASSERT_EQ(*tx.os_fac, 2);
    ASSERT_EQ(tx.entries.size(), 2u);
    ASSERT_EQ(tx.entries[1].name,    "Status");
    ASSERT_EQ(tx.entries[1].bit_len, 8u);
}

TEST(ESIParser, pdos_synthesize_legacy_mapping_objects)
{
    // Fixture's <RxPdo Index=0x1600> and <TxPdo Index=0x1A00> are not in
    // <Dictionary>/<Objects>. The parser should synthesize them plus the
    // 0x1C12/0x1C13 SM-assignment objects for legacy callers.
    ESI::Parser parser;
    auto dictionary = parser.loadFile("kickcat_esi_test_sm_fmmu.xml");

    auto [obj_1600, _e1] = findObject(dictionary, 0x1600, 0);
    ASSERT_NE(obj_1600, nullptr);
    ASSERT_EQ(obj_1600->code, CoE::ObjectCode::RECORD);
    ASSERT_EQ(obj_1600->entries.size(), 2u);  // size + 1 entry
    uint8_t entry_count;
    std::memcpy(&entry_count, obj_1600->entries[0].data, 1);
    ASSERT_EQ(entry_count, 1u);
    uint32_t packed;
    std::memcpy(&packed, obj_1600->entries[1].data, 4);
    ASSERT_EQ(packed, (0x7000u << 16) | (1u << 8) | 16u);

    auto [obj_1A00, _e2] = findObject(dictionary, 0x1A00, 0);
    ASSERT_NE(obj_1A00, nullptr);
    ASSERT_EQ(obj_1A00->entries.size(), 3u);  // size + 2 entries

    auto [obj_1C12, _e3] = findObject(dictionary, 0x1C12, 0);
    ASSERT_NE(obj_1C12, nullptr);
    ASSERT_EQ(obj_1C12->code, CoE::ObjectCode::ARRAY);
    ASSERT_EQ(obj_1C12->entries.size(), 2u);  // size + 1 assigned PDO
    uint16_t assigned_rx;
    std::memcpy(&assigned_rx, obj_1C12->entries[1].data, 2);
    ASSERT_EQ(assigned_rx, 0x1600u);

    auto [obj_1C13, _e4] = findObject(dictionary, 0x1C13, 0);
    ASSERT_NE(obj_1C13, nullptr);
    ASSERT_EQ(obj_1C13->entries.size(), 2u);
    uint16_t assigned_tx;
    std::memcpy(&assigned_tx, obj_1C13->entries[1].data, 2);
    ASSERT_EQ(assigned_tx, 0x1A00u);
}

TEST(ESIParser, pdo_entry_is_authoritative_over_explicit_mapping_object)
{
    // ETG.2010 Tables 14/15: the PDO mapping is defined by <Pdo>/<Entry>, so for a
    // mapping-object index (0x16xx/0x1Axx) the <Entry>-derived mapping is
    // authoritative over a conflicting explicit <Object> (whose DefaultData may be
    // a vendor typo, e.g. Beckhoff EL4004). The explicit object here is a stand-in
    // that must be replaced by the <Entry> version.
    char const* xml = R"(<?xml version="1.0"?>
        <EtherCATInfo>
            <Vendor><Id>#x1</Id><Name>V</Name></Vendor>
            <Descriptions><Devices><Device>
                <Type ProductCode="#x1" RevisionNo="#x1">T</Type>
                <Profile><ProfileNo>0</ProfileNo>
                    <Dictionary>
                        <DataTypes><DataType><Name>UDINT</Name><BitSize>32</BitSize></DataType></DataTypes>
                        <Objects>
                            <Object>
                                <Index>#x1600</Index>
                                <Name>Explicit RxPDO map</Name>
                                <Type>UDINT</Type>
                                <BitSize>32</BitSize>
                                <Flags><Access>rw</Access></Flags>
                            </Object>
                        </Objects>
                    </Dictionary>
                </Profile>
                <RxPdo Sm="2">
                    <Index>#x1600</Index>
                    <Name>Auto-generated map</Name>
                    <Entry><Index>#x7000</Index><SubIndex>1</SubIndex><BitLen>16</BitLen></Entry>
                </RxPdo>
            </Device></Devices></Descriptions>
        </EtherCATInfo>)";

    ESI::Parser parser;
    auto dictionary = parser.loadString(xml);
    auto [obj, _] = findObject(dictionary, 0x1600, 0);
    ASSERT_NE(obj, nullptr);
    ASSERT_EQ(obj->name, "Auto-generated map");          // <Entry>-derived object replaced the explicit one
    ASSERT_EQ(obj->code, CoE::ObjectCode::RECORD);

    auto [_o, entry1] = findObject(dictionary, 0x1600, 1);
    ASSERT_NE(entry1, nullptr);
    uint32_t packed;
    std::memcpy(&packed, entry1->data, 4);
    ASSERT_EQ(packed, (0x7000u << 16) | (1u << 8) | 16u);  // mapping from <Entry>
}

TEST(ESIParser, sm_assignment_is_authoritative_over_explicit_over_assignment)
{
    // ETG.2010 Table 14: only PDOs "mapped by default" (those carrying @Sm) belong
    // to a SyncManager's default assignment, at 0x1C10 + SM index. When an explicit
    // 0x1C12 over-assigns a phantom PDO with no <RxPdo> (e.g. the shared dictionary
    // in Beckhoff EL4004), the @Sm-derived assignment replaces it.
    char const* xml = R"(<?xml version="1.0"?>
        <EtherCATInfo>
            <Vendor><Id>#x1</Id><Name>V</Name></Vendor>
            <Descriptions><Devices><Device>
                <Type ProductCode="#x1" RevisionNo="#x1">T</Type>
                <Profile><ProfileNo>0</ProfileNo>
                    <Dictionary>
                        <DataTypes>
                            <DataType><Name>USINT</Name><BitSize>8</BitSize></DataType>
                            <DataType><Name>UINT</Name><BitSize>16</BitSize></DataType>
                            <DataType>
                                <Name>UINT_ARR2</Name><BaseType>UINT</BaseType><BitSize>32</BitSize>
                                <ArrayInfo><LBound>1</LBound><Elements>2</Elements></ArrayInfo>
                            </DataType>
                            <DataType>
                                <Name>DT1C12</Name><BitSize>48</BitSize>
                                <SubItem><SubIdx>0</SubIdx><Name>Count</Name><Type>USINT</Type><BitSize>8</BitSize><BitOffs>0</BitOffs></SubItem>
                                <SubItem><Name>Elements</Name><Type>UINT_ARR2</Type><BitSize>32</BitSize><BitOffs>16</BitOffs></SubItem>
                            </DataType>
                        </DataTypes>
                        <Objects>
                            <Object>
                                <Index>#x1C12</Index>
                                <Name>Explicit over-assignment</Name>
                                <Type>DT1C12</Type>
                                <BitSize>48</BitSize>
                                <Info>
                                    <SubItem><Name>Count</Name><Info><DefaultData>02</DefaultData></Info></SubItem>
                                    <SubItem><Name>E1</Name><Info><DefaultData>0016</DefaultData></Info></SubItem>
                                    <SubItem><Name>E2</Name><Info><DefaultData>0116</DefaultData></Info></SubItem>
                                </Info>
                            </Object>
                        </Objects>
                    </Dictionary>
                </Profile>
                <RxPdo Sm="2">
                    <Index>#x1600</Index>
                    <Name>Outputs</Name>
                    <Entry><Index>#x7000</Index><SubIndex>1</SubIndex><BitLen>16</BitLen></Entry>
                </RxPdo>
            </Device></Devices></Descriptions>
        </EtherCATInfo>)";

    ESI::Parser parser;
    auto dictionary = parser.loadString(xml);

    auto [obj, e0] = findObject(dictionary, 0x1C12, 0);
    ASSERT_NE(obj, nullptr);
    ASSERT_EQ(obj->name, "RxPDO assign");        // @Sm-derived object replaced the explicit one
    ASSERT_EQ(obj->entries.size(), 2u);          // count + 1 assigned PDO (phantom 0x1601 dropped)
    uint8_t count;
    std::memcpy(&count, e0->data, 1);
    ASSERT_EQ(count, 1u);
    uint16_t assigned;
    std::memcpy(&assigned, obj->entries[1].data, 2);
    ASSERT_EQ(assigned, 0x1600u);
}

TEST(ESIParser, mapping_target_subindex_reconciled_against_object)
{
    // ETG.1000.6 Tables 74/75: a mapping entry references object:subindex. Here the
    // <Entry> names 0x7000:17 (a vendor typo, as in EL4004 rev>=0x13) but object
    // 0x7000 declares its 16-bit value at sub 1. The object dictionary is the
    // authority, so the mapping must be retargeted to the resolvable sub 1.
    char const* xml = R"(<?xml version="1.0"?>
        <EtherCATInfo>
            <Vendor><Id>#x1</Id><Name>V</Name></Vendor>
            <Descriptions><Devices><Device>
                <Type ProductCode="#x1" RevisionNo="#x1">T</Type>
                <Profile><ProfileNo>0</ProfileNo>
                    <Dictionary>
                        <DataTypes>
                            <DataType><Name>USINT</Name><BitSize>8</BitSize></DataType>
                            <DataType><Name>UINT</Name><BitSize>16</BitSize></DataType>
                            <DataType>
                                <Name>DT7000</Name><BitSize>24</BitSize>
                                <SubItem><SubIdx>0</SubIdx><Name>Max</Name><Type>USINT</Type><BitSize>8</BitSize><BitOffs>0</BitOffs></SubItem>
                                <SubItem><SubIdx>1</SubIdx><Name>Value</Name><Type>UINT</Type><BitSize>16</BitSize><BitOffs>8</BitOffs></SubItem>
                            </DataType>
                        </DataTypes>
                        <Objects>
                            <Object><Index>#x7000</Index><Name>AO</Name><Type>DT7000</Type><BitSize>24</BitSize></Object>
                        </Objects>
                    </Dictionary>
                </Profile>
                <RxPdo Sm="2">
                    <Index>#x1600</Index>
                    <Name>Outputs</Name>
                    <Entry><Index>#x7000</Index><SubIndex>17</SubIndex><BitLen>16</BitLen></Entry>
                </RxPdo>
            </Device></Devices></Descriptions>
        </EtherCATInfo>)";

    ESI::Parser parser;
    auto dictionary = parser.loadString(xml);

    auto [obj, entry1] = findObject(dictionary, 0x1600, 1);
    ASSERT_NE(entry1, nullptr);
    uint32_t packed;
    std::memcpy(&packed, entry1->data, 4);
    ASSERT_EQ(packed, (0x7000u << 16) | (1u << 8) | 16u);  // retargeted 0x7000:17 -> 0x7000:1
}

TEST(ESIParser, loadDevice_parses_eeprom)
{
    ESI::Parser parser;
    ESI::Device device = parser.loadDevice("kickcat_esi_test_sm_fmmu.xml");

    ASSERT_TRUE(device.eeprom.has_value());
    auto const& eep = *device.eeprom;
    ASSERT_TRUE(eep.assign_to_pdi);
    ASSERT_TRUE(eep.byte_size.has_value());
    ASSERT_EQ(*eep.byte_size, 2048);
    ASSERT_EQ(eep.config_data.size(), 16u);
    ASSERT_EQ(eep.config_data[0], 0x05);
    ASSERT_EQ(eep.bootstrap.size(), 8u);
    ASSERT_EQ(eep.categories.size(), 2u);

    ASSERT_EQ(eep.categories[0].cat_no, 30);
    ASSERT_TRUE(eep.categories[0].data_string.has_value());
    ASSERT_EQ(*eep.categories[0].data_string, "BootCfg");
    ASSERT_FALSE(eep.categories[0].preserve_online_data);

    ASSERT_EQ(eep.categories[1].cat_no, 40);
    ASSERT_TRUE(eep.categories[1].preserve_online_data);
    ASSERT_EQ(eep.categories[1].data.size(), 4u);
    ASSERT_EQ(eep.categories[1].data[0], 0xDE);
}

TEST(ESIParser, loadDevice_eeprom_raw_data_form)
{
    // Eeprom can also carry a raw image directly as <Data>; the structured
    // form (ByteSize/ConfigData/etc.) must not be required in that case.
    char const* xml = R"(<?xml version="1.0"?>
        <EtherCATInfo>
            <Vendor><Id>#x1</Id><Name>V</Name></Vendor>
            <Descriptions><Devices><Device>
                <Type ProductCode="#x1" RevisionNo="#x1">T</Type>
                <Profile><ProfileNo>0</ProfileNo>
                    <Dictionary>
                        <DataTypes><DataType><Name>UDINT</Name><BitSize>32</BitSize></DataType></DataTypes>
                        <Objects>
                            <Object><Index>#x1000</Index><Name>X</Name><Type>UDINT</Type><BitSize>32</BitSize></Object>
                        </Objects>
                    </Dictionary>
                </Profile>
                <Eeprom>
                    <Data>0102030405060708</Data>
                </Eeprom>
            </Device></Devices></Descriptions>
        </EtherCATInfo>)";

    ESI::Parser parser;
    ESI::Device device = parser.loadDeviceString(xml);
    ASSERT_TRUE(device.eeprom.has_value());
    ASSERT_EQ(device.eeprom->raw_data.size(), 8u);
    ASSERT_EQ(device.eeprom->raw_data[7], 0x08u);
    ASSERT_FALSE(device.eeprom->byte_size.has_value());
}

TEST(ESIParser, loadDevice_parses_dc)
{
    ESI::Parser parser;
    ESI::Device device = parser.loadDevice("kickcat_esi_test_sm_fmmu.xml");

    ASSERT_TRUE(device.dc.has_value());
    auto const& dc = *device.dc;
    ASSERT_TRUE (dc.potential_reference_clock);
    ASSERT_FALSE(dc.pdo_oversampling);
    ASSERT_FALSE(dc.external_ref_clock);
    ASSERT_EQ(dc.op_modes.size(), 2u);

    auto const& synchron = dc.op_modes[0];
    ASSERT_EQ(synchron.name,            "Synchron");
    ASSERT_EQ(synchron.desc,            "SM-Synchron");
    ASSERT_EQ(synchron.assign_activate, 0x300u);
    ASSERT_FALSE(synchron.activate_additional.has_value());

    ASSERT_TRUE(synchron.cycle_time[0].has_value());
    ASSERT_EQ(synchron.cycle_time[0]->value, 1000000);
    ASSERT_TRUE(synchron.cycle_time[0]->factor.has_value());
    ASSERT_EQ(*synchron.cycle_time[0]->factor, 1);

    ASSERT_TRUE(synchron.shift_time[0].has_value());
    ASSERT_EQ(synchron.shift_time[0]->value, 100);
    ASSERT_TRUE(synchron.shift_time[0]->input.has_value());
    ASSERT_TRUE(*synchron.shift_time[0]->input);
    ASSERT_TRUE(synchron.shift_time[0]->output_delay_time.has_value());
    ASSERT_EQ(*synchron.shift_time[0]->output_delay_time, 500);

    // <Sm No="3"> with one oversampled <Pdo OSFac="2">#x1A00</Pdo>; the obsolete
    // <SyncType> child is present in the fixture but must not be surfaced.
    ASSERT_EQ(synchron.sm_configs.size(), 1u);
    ASSERT_EQ(synchron.sm_configs[0].no,           3);
    ASSERT_EQ(synchron.sm_configs[0].pdos.size(),  1u);
    ASSERT_EQ(synchron.sm_configs[0].pdos[0].index, 0x1A00u);
    ASSERT_TRUE(synchron.sm_configs[0].pdos[0].os_fac.has_value());
    ASSERT_EQ(*synchron.sm_configs[0].pdos[0].os_fac, 2);

    auto const& freerun = dc.op_modes[1];
    ASSERT_EQ(freerun.name,            "FreeRun");
    ASSERT_EQ(freerun.assign_activate, 0u);
    ASSERT_FALSE(freerun.cycle_time[0].has_value());
    ASSERT_TRUE(freerun.sm_configs.empty());
}

TEST(ESIParser, eeprom_dc_absent_when_blocks_missing)
{
    ESI::Parser parser;
    ESI::Device device = parser.loadDevice("kickcat_esi_test_multi_device.xml");

    ASSERT_FALSE(device.eeprom.has_value());
    ASSERT_FALSE(device.dc.has_value());
}

TEST(ESIParser, throws_on_unknown_fmmu_text)
{
    char const* xml = R"(<?xml version="1.0"?>
        <EtherCATInfo>
            <Vendor><Id>#x1</Id><Name>V</Name></Vendor>
            <Descriptions><Devices><Device>
                <Type ProductCode="#x1" RevisionNo="#x1">T</Type>
                <Profile><ProfileNo>0</ProfileNo>
                    <Dictionary>
                        <DataTypes><DataType><Name>UDINT</Name><BitSize>32</BitSize></DataType></DataTypes>
                        <Objects>
                            <Object><Index>#x1000</Index><Name>X</Name><Type>UDINT</Type><BitSize>32</BitSize></Object>
                        </Objects>
                    </Dictionary>
                </Profile>
                <Fmmu>SomeUnknownThing</Fmmu>
            </Device></Devices></Descriptions>
        </EtherCATInfo>)";

    ESI::Parser parser;
    try
    {
        (void) parser.loadString(xml);
        FAIL() << "expected invalid_argument";
    }
    catch (std::invalid_argument const& e)
    {
        std::string msg = e.what();
        ASSERT_NE(msg.find("SomeUnknownThing"), std::string::npos) << msg;
    }
}

TEST(ESIParser, string_default_data_not_reversed)
{
    // <DefaultData>4B69636B43415421</DefaultData> is hex for "KickCAT!".
    // The bytes must land in natural order (was reversed pre-fix).
    ESI::Parser parser;
    auto dictionary = parser.loadFile("kickcat_esi_test_basic.xml");
    auto [object, entry] = findObject(dictionary, 0x1008, 0);
    ASSERT_NE(object, nullptr);
    ASSERT_EQ(entry->type, CoE::DataType::VISIBLE_STRING);
    ASSERT_NE(entry->data, nullptr);
    ASSERT_EQ(std::memcmp(entry->data, "KickCAT!", 8), 0);
}

TEST(ESIParser, x1C00_not_duplicated_when_explicit)
{
    // ESI declares 0x1C00 explicitly. The synthesised one must not appear.
    char const* xml = R"(<?xml version="1.0"?>
        <EtherCATInfo>
            <Vendor><Id>#x1</Id><Name>V</Name></Vendor>
            <Descriptions><Devices><Device>
                <Type ProductCode="#x1" RevisionNo="#x1">T</Type>
                <Profile><ProfileNo>0</ProfileNo>
                    <Dictionary>
                        <DataTypes>
                            <DataType><Name>USINT</Name><BitSize>8</BitSize></DataType>
                            <DataType>
                                <Name>SM_ARR</Name><BaseType>USINT</BaseType><BitSize>16</BitSize>
                                <ArrayInfo><LBound>1</LBound><Elements>1</Elements></ArrayInfo>
                            </DataType>
                            <DataType>
                                <Name>DT1C00</Name><BitSize>16</BitSize>
                                <SubItem><SubIdx>0</SubIdx><Name>Count</Name><Type>USINT</Type><BitSize>8</BitSize><BitOffs>0</BitOffs></SubItem>
                            </DataType>
                        </DataTypes>
                        <Objects>
                            <Object><Index>#x1C00</Index><Name>Explicit SM types</Name><Type>DT1C00</Type><BitSize>16</BitSize></Object>
                        </Objects>
                    </Dictionary>
                </Profile>
                <Sm StartAddress="#x1000" ControlByte="#x26" Enable="1">MBoxOut</Sm>
            </Device></Devices></Descriptions>
        </EtherCATInfo>)";

    ESI::Parser parser;
    auto dictionary = parser.loadString(xml);
    int count = 0;
    for (auto const& o : dictionary)
    {
        if (o.index == 0x1C00) { ++count; }
    }
    ASSERT_EQ(count, 1);
    auto [obj, _] = findObject(dictionary, 0x1C00, 0);
    ASSERT_NE(obj, nullptr);
    ASSERT_EQ(obj->name, "Explicit SM types");
}

TEST(ESIParser, array_with_255_elements_does_not_hang)
{
    // PR4 reviewer found: uint8_t loop counter wraps at elements=255 (real
    // ETG examples have this). Pin the fix.
    char const* xml = R"(<?xml version="1.0"?>
        <EtherCATInfo>
            <Vendor><Id>#x1</Id><Name>V</Name></Vendor>
            <Descriptions><Devices><Device>
                <Type ProductCode="#x1" RevisionNo="#x1">T</Type>
                <Profile><ProfileNo>0</ProfileNo>
                    <Dictionary>
                        <DataTypes>
                            <DataType><Name>USINT</Name><BitSize>8</BitSize></DataType>
                            <DataType>
                                <Name>BigArr</Name><BaseType>USINT</BaseType><BitSize>2040</BitSize>
                                <ArrayInfo><LBound>1</LBound><Elements>255</Elements></ArrayInfo>
                            </DataType>
                            <DataType>
                                <Name>DT</Name><BitSize>2048</BitSize>
                                <SubItem><SubIdx>0</SubIdx><Name>n</Name><Type>USINT</Type><BitSize>8</BitSize><BitOffs>0</BitOffs></SubItem>
                                <SubItem><SubIdx>1</SubIdx><Name>arr</Name><Type>BigArr</Type><BitSize>2040</BitSize><BitOffs>8</BitOffs></SubItem>
                            </DataType>
                        </DataTypes>
                        <Objects>
                            <Object><Index>#x2000</Index><Name>BigRecord</Name><Type>DT</Type><BitSize>2048</BitSize></Object>
                        </Objects>
                    </Dictionary>
                </Profile>
            </Device></Devices></Descriptions>
        </EtherCATInfo>)";

    ESI::Parser parser;
    auto dictionary = parser.loadString(xml);
    auto [obj, _] = findObject(dictionary, 0x2000, 0);
    ASSERT_NE(obj, nullptr);
    // SubIndex 0 + 255 array elements
    ASSERT_EQ(obj->entries.size(), 256u);
}

TEST(ESIParser, array_with_more_than_255_elements_throws)
{
    char const* xml = R"(<?xml version="1.0"?>
        <EtherCATInfo>
            <Vendor><Id>#x1</Id><Name>V</Name></Vendor>
            <Descriptions><Devices><Device>
                <Type ProductCode="#x1" RevisionNo="#x1">T</Type>
                <Profile><ProfileNo>0</ProfileNo>
                    <Dictionary>
                        <DataTypes>
                            <DataType><Name>USINT</Name><BitSize>8</BitSize></DataType>
                            <DataType>
                                <Name>TooBig</Name><BaseType>USINT</BaseType><BitSize>2048</BitSize>
                                <ArrayInfo><LBound>1</LBound><Elements>256</Elements></ArrayInfo>
                            </DataType>
                            <DataType>
                                <Name>DT</Name><BitSize>2056</BitSize>
                                <SubItem><SubIdx>0</SubIdx><Name>n</Name><Type>USINT</Type><BitSize>8</BitSize><BitOffs>0</BitOffs></SubItem>
                                <SubItem><SubIdx>1</SubIdx><Name>arr</Name><Type>TooBig</Type><BitSize>2048</BitSize><BitOffs>8</BitOffs></SubItem>
                            </DataType>
                        </DataTypes>
                        <Objects>
                            <Object><Index>#x2000</Index><Name>R</Name><Type>DT</Type><BitSize>2056</BitSize></Object>
                        </Objects>
                    </Dictionary>
                </Profile>
            </Device></Devices></Descriptions>
        </EtherCATInfo>)";

    ESI::Parser parser;
    ASSERT_THROW((void) parser.loadString(xml), std::invalid_argument);
}

TEST(ESIParser, throws_on_odd_length_hex_binary)
{
    char const* xml = R"(<?xml version="1.0"?>
        <EtherCATInfo>
            <Vendor><Id>#x1</Id><Name>V</Name></Vendor>
            <Descriptions><Devices><Device>
                <Type ProductCode="#x1" RevisionNo="#x1">T</Type>
                <Profile><ProfileNo>0</ProfileNo>
                    <Dictionary>
                        <DataTypes><DataType><Name>UDINT</Name><BitSize>32</BitSize></DataType></DataTypes>
                        <Objects>
                            <Object>
                                <Index>#x1000</Index>
                                <Name>X</Name>
                                <Type>UDINT</Type>
                                <BitSize>32</BitSize>
                                <Info><DefaultData>012</DefaultData></Info>
                            </Object>
                        </Objects>
                    </Dictionary>
                </Profile>
            </Device></Devices></Descriptions>
        </EtherCATInfo>)";

    ESI::Parser parser;
    try
    {
        (void) parser.loadString(xml);
        FAIL() << "expected invalid_argument";
    }
    catch (std::invalid_argument const& e)
    {
        std::string msg = e.what();
        ASSERT_NE(msg.find("odd length"), std::string::npos) << msg;
    }
}

TEST(ESIParser, throws_on_numeric_overflow)
{
    // SubIndex is uint8_t; #xFF01 overflows -> must throw, not silently wrap.
    char const* xml = R"(<?xml version="1.0"?>
        <EtherCATInfo>
            <Vendor><Id>#x1</Id><Name>V</Name></Vendor>
            <Descriptions><Devices><Device>
                <Type ProductCode="#x1" RevisionNo="#x1">T</Type>
                <Profile><ProfileNo>0</ProfileNo>
                    <Dictionary>
                        <DataTypes><DataType><Name>UDINT</Name><BitSize>32</BitSize></DataType></DataTypes>
                        <Objects>
                            <Object><Index>#x1000</Index><Name>X</Name><Type>UDINT</Type><BitSize>32</BitSize></Object>
                        </Objects>
                    </Dictionary>
                </Profile>
                <Mailbox>
                    <CoE>
                        <InitCmd>
                            <Transition>PS</Transition>
                            <Index>#x1000</Index>
                            <SubIndex>#xFF01</SubIndex>
                            <Data>00</Data>
                        </InitCmd>
                    </CoE>
                </Mailbox>
            </Device></Devices></Descriptions>
        </EtherCATInfo>)";

    ESI::Parser parser;
    try
    {
        (void) parser.loadString(xml);
        FAIL() << "expected invalid_argument";
    }
    catch (std::invalid_argument const& e)
    {
        std::string msg = e.what();
        ASSERT_NE(msg.find("out of range"), std::string::npos) << msg;
    }
}

TEST(ESIParser, throws_on_missing_vendor_id)
{
    char const* xml = R"(<?xml version="1.0"?>
        <EtherCATInfo>
            <Vendor><Name>V</Name></Vendor>
            <Descriptions><Devices><Device>
                <Type ProductCode="#x1" RevisionNo="#x1">T</Type>
                <Profile><ProfileNo>0</ProfileNo>
                    <Dictionary>
                        <DataTypes><DataType><Name>UDINT</Name><BitSize>32</BitSize></DataType></DataTypes>
                        <Objects>
                            <Object><Index>#x1000</Index><Name>X</Name><Type>UDINT</Type><BitSize>32</BitSize></Object>
                        </Objects>
                    </Dictionary>
                </Profile>
            </Device></Devices></Descriptions>
        </EtherCATInfo>)";

    ESI::Parser parser;
    try
    {
        (void) parser.loadString(xml);
        FAIL() << "expected invalid_argument";
    }
    catch (std::invalid_argument const& e)
    {
        std::string msg = e.what();
        ASSERT_NE(msg.find("Id"),     std::string::npos) << msg;
        ASSERT_NE(msg.find("Vendor"), std::string::npos) << msg;
    }
}

TEST(ESIParser, empty_vendor_id_element_tolerated)
{
    // <Id></Id> (present but empty) should default to vendor_id=0 — real
    // vendor catalogs ship placeholder empty Id elements.
    char const* xml = R"(<?xml version="1.0"?>
        <EtherCATInfo>
            <Vendor><Id></Id><Name>V</Name></Vendor>
            <Descriptions><Devices><Device>
                <Type ProductCode="#x1" RevisionNo="#x1">T</Type>
            </Device></Devices></Descriptions>
        </EtherCATInfo>)";

    ESI::Parser parser;
    ESI::Device device = parser.loadDeviceString(xml);
    ASSERT_EQ(device.vendor_id, 0u);
}

TEST(ESIParser, throws_on_array_bitsize_not_divisible_by_elements)
{
    // BitSize=10, Elements=3 -> 10/3=3 truncates 1 bit silently if unchecked.
    char const* xml = R"(<?xml version="1.0"?>
        <EtherCATInfo>
            <Vendor><Id>#x1</Id><Name>V</Name></Vendor>
            <Descriptions><Devices><Device>
                <Type ProductCode="#x1" RevisionNo="#x1">T</Type>
                <Profile><ProfileNo>0</ProfileNo>
                    <Dictionary>
                        <DataTypes>
                            <DataType><Name>USINT</Name><BitSize>8</BitSize></DataType>
                            <DataType>
                                <Name>Odd</Name><BaseType>USINT</BaseType><BitSize>10</BitSize>
                                <ArrayInfo><LBound>1</LBound><Elements>3</Elements></ArrayInfo>
                            </DataType>
                            <DataType>
                                <Name>DT</Name><BitSize>18</BitSize>
                                <SubItem><SubIdx>0</SubIdx><Name>n</Name><Type>USINT</Type><BitSize>8</BitSize><BitOffs>0</BitOffs></SubItem>
                                <SubItem><SubIdx>1</SubIdx><Name>arr</Name><Type>Odd</Type><BitSize>10</BitSize><BitOffs>8</BitOffs></SubItem>
                            </DataType>
                        </DataTypes>
                        <Objects>
                            <Object><Index>#x2000</Index><Name>R</Name><Type>DT</Type><BitSize>18</BitSize></Object>
                        </Objects>
                    </Dictionary>
                </Profile>
            </Device></Devices></Descriptions>
        </EtherCATInfo>)";

    ESI::Parser parser;
    try
    {
        (void) parser.loadString(xml);
        FAIL() << "expected invalid_argument";
    }
    catch (std::invalid_argument const& e)
    {
        std::string msg = e.what();
        ASSERT_NE(msg.find("divisible"), std::string::npos) << msg;
    }
}

TEST(ESIParser, throws_on_duplicate_rx_pdo_index)
{
    char const* xml = R"(<?xml version="1.0"?>
        <EtherCATInfo>
            <Vendor><Id>#x1</Id><Name>V</Name></Vendor>
            <Descriptions><Devices><Device>
                <Type ProductCode="#x1" RevisionNo="#x1">T</Type>
                <RxPdo Sm="2"><Index>#x1600</Index><Name>A</Name></RxPdo>
                <RxPdo Sm="2"><Index>#x1600</Index><Name>B</Name></RxPdo>
            </Device></Devices></Descriptions>
        </EtherCATInfo>)";

    ESI::Parser parser;
    try
    {
        (void) parser.loadString(xml);
        FAIL() << "expected invalid_argument";
    }
    catch (std::invalid_argument const& e)
    {
        std::string msg = e.what();
        ASSERT_NE(msg.find("duplicate"), std::string::npos) << msg;
        ASSERT_NE(msg.find("0x1600"),    std::string::npos) << msg;
    }
}

TEST(ESIParser, throws_on_eeprom_data_and_byte_size_both_present)
{
    char const* xml = R"(<?xml version="1.0"?>
        <EtherCATInfo>
            <Vendor><Id>#x1</Id><Name>V</Name></Vendor>
            <Descriptions><Devices><Device>
                <Type ProductCode="#x1" RevisionNo="#x1">T</Type>
                <Eeprom>
                    <Data>0102</Data>
                    <ByteSize>2048</ByteSize>
                </Eeprom>
            </Device></Devices></Descriptions>
        </EtherCATInfo>)";

    ESI::Parser parser;
    try
    {
        (void) parser.loadString(xml);
        FAIL() << "expected invalid_argument";
    }
    catch (std::invalid_argument const& e)
    {
        std::string msg = e.what();
        ASSERT_NE(msg.find("mutually exclusive"), std::string::npos) << msg;
    }
}

TEST(ESIParser, throws_on_dictionary_without_objects)
{
    char const* xml = R"(<?xml version="1.0"?>
        <EtherCATInfo>
            <Vendor><Id>#x1</Id><Name>V</Name></Vendor>
            <Descriptions><Devices><Device>
                <Type ProductCode="#x1" RevisionNo="#x1">T</Type>
                <Profile><ProfileNo>0</ProfileNo>
                    <Dictionary>
                        <DataTypes><DataType><Name>UDINT</Name><BitSize>32</BitSize></DataType></DataTypes>
                    </Dictionary>
                </Profile>
            </Device></Devices></Descriptions>
        </EtherCATInfo>)";

    ESI::Parser parser;
    ASSERT_THROW((void) parser.loadString(xml), std::invalid_argument);
}

TEST(ESIParser, dtypes_missing_gives_contextual_error_for_non_basic_type)
{
    char const* xml = R"(<?xml version="1.0"?>
        <EtherCATInfo>
            <Vendor><Id>#x1</Id><Name>V</Name></Vendor>
            <Descriptions><Devices><Device>
                <Type ProductCode="#x1" RevisionNo="#x1">T</Type>
                <Profile><ProfileNo>0</ProfileNo>
                    <Dictionary>
                        <Objects>
                            <Object><Index>#x2000</Index><Name>X</Name><Type>MyRecord</Type><BitSize>16</BitSize></Object>
                        </Objects>
                    </Dictionary>
                </Profile>
            </Device></Devices></Descriptions>
        </EtherCATInfo>)";

    ESI::Parser parser;
    try
    {
        (void) parser.loadString(xml);
        FAIL() << "expected invalid_argument";
    }
    catch (std::invalid_argument const& e)
    {
        std::string msg = e.what();
        ASSERT_NE(msg.find("DataTypes"),    std::string::npos) << msg;
        ASSERT_NE(msg.find("Object 0x2000"), std::string::npos) << msg;
    }
}

TEST(ESIParser, signed_type_accepts_unsigned_bit_pattern)
{
    // SoE InitCmd IDN is int32_t in the schema, but real ESIs use #xFFFFFFFF
    // as a bit-pattern sentinel meaning -1. The narrower accepts this.
    char const* xml = R"(<?xml version="1.0"?>
        <EtherCATInfo>
            <Vendor><Id>#x1</Id><Name>V</Name></Vendor>
            <Descriptions><Devices><Device>
                <Type ProductCode="#x1" RevisionNo="#x1">T</Type>
                <Mailbox>
                    <SoE>
                        <InitCmd>
                            <Transition>PS</Transition>
                            <IDN>#xFFFFFFFF</IDN>
                            <Data>00</Data>
                        </InitCmd>
                    </SoE>
                </Mailbox>
            </Device></Devices></Descriptions>
        </EtherCATInfo>)";

    ESI::Parser parser;
    ESI::Device device = parser.loadDeviceString(xml);
    ASSERT_TRUE(device.mailbox.has_value());
    ASSERT_TRUE(device.mailbox->soe.has_value());
    ASSERT_EQ(device.mailbox->soe->init_cmds.size(), 1u);
    ASSERT_EQ(device.mailbox->soe->init_cmds[0].idn, -1);
}

TEST(ESIParser, stoll_overflow_carries_esi_context)
{
    // A decimal number exceeding INT64_MAX must surface ESI context, not the
    // raw std::out_of_range message.
    char const* xml = R"(<?xml version="1.0"?>
        <EtherCATInfo>
            <Vendor><Id>99999999999999999999999</Id><Name>V</Name></Vendor>
            <Descriptions><Devices><Device>
                <Type ProductCode="#x1" RevisionNo="#x1">T</Type>
            </Device></Devices></Descriptions>
        </EtherCATInfo>)";

    ESI::Parser parser;
    try
    {
        (void) parser.loadString(xml);
        FAIL() << "expected invalid_argument";
    }
    catch (std::invalid_argument const& e)
    {
        std::string msg = e.what();
        ASSERT_NE(msg.find("Vendor/Id"), std::string::npos) << msg;
    }
}

TEST(ESIParser, non_hex_data_carries_esi_context)
{
    // <Data>GGGG</Data>: per-byte parse fails. Error must name the element
    // and offending pair, not just raw "stoi".
    char const* xml = R"(<?xml version="1.0"?>
        <EtherCATInfo>
            <Vendor><Id>#x1</Id><Name>V</Name></Vendor>
            <Descriptions><Devices><Device>
                <Type ProductCode="#x1" RevisionNo="#x1">T</Type>
                <Profile><ProfileNo>0</ProfileNo>
                    <Dictionary>
                        <DataTypes><DataType><Name>UDINT</Name><BitSize>32</BitSize></DataType></DataTypes>
                        <Objects>
                            <Object>
                                <Index>#x1000</Index><Name>X</Name><Type>UDINT</Type><BitSize>32</BitSize>
                                <Info><DefaultData>GGGGGGGG</DefaultData></Info>
                            </Object>
                        </Objects>
                    </Dictionary>
                </Profile>
            </Device></Devices></Descriptions>
        </EtherCATInfo>)";

    ESI::Parser parser;
    try
    {
        (void) parser.loadString(xml);
        FAIL() << "expected invalid_argument";
    }
    catch (std::invalid_argument const& e)
    {
        std::string msg = e.what();
        ASSERT_NE(msg.find("non-hex"),    std::string::npos) << msg;
        ASSERT_NE(msg.find("DefaultData"), std::string::npos) << msg;
    }
}

TEST(ESIParser, bypass_sites_now_throw_on_overflow)
{
    // ProfileNo > UINT16_MAX must throw via parseHexDec<uint16_t> now.
    char const* xml = R"(<?xml version="1.0"?>
        <EtherCATInfo>
            <Vendor><Id>#x1</Id><Name>V</Name></Vendor>
            <Descriptions><Devices><Device>
                <Type ProductCode="#x1" RevisionNo="#x1">T</Type>
                <Profile><ProfileNo>#x10000</ProfileNo></Profile>
            </Device></Devices></Descriptions>
        </EtherCATInfo>)";

    ESI::Parser parser;
    try
    {
        (void) parser.loadString(xml);
        FAIL() << "expected invalid_argument";
    }
    catch (std::invalid_argument const& e)
    {
        std::string msg = e.what();
        ASSERT_NE(msg.find("Profile/ProfileNo"), std::string::npos) << msg;
        ASSERT_NE(msg.find("out of range"),       std::string::npos) << msg;
    }
}

TEST(ESIParser, throws_on_empty_document)
{
    ESI::Parser parser;
    ASSERT_THROW((void) parser.loadString("<?xml version=\"1.0\"?>\n<!-- nothing -->"),
                 std::invalid_argument);
}

TEST(ESIParser, profile_no_reset_between_loads)
{
    ESI::Parser parser;
    ESI::Device d1 = parser.loadDevice("kickcat_esi_test_basic.xml");
    ASSERT_EQ(d1.profile_no, 5001u);

    // Second device has no <Profile> — profile_no must NOT carry over.
    char const* xml = R"(<?xml version="1.0"?>
        <EtherCATInfo>
            <Vendor><Id>#x1</Id><Name>V</Name></Vendor>
            <Descriptions><Devices><Device>
                <Type ProductCode="#x1" RevisionNo="#x1">NoProfile</Type>
                <Name>NoProfile</Name>
            </Device></Devices></Descriptions>
        </EtherCATInfo>)";
    ESI::Device d2 = parser.loadDeviceString(xml);
    ASSERT_EQ(d2.profile_no, 0u);
    ASSERT_STREQ(parser.profile(), "");
}

TEST(ESIParser, device_without_profile_parses)
{
    char const* xml = R"(<?xml version="1.0"?>
        <EtherCATInfo>
            <Vendor><Id>#x1</Id><Name>V</Name></Vendor>
            <Descriptions><Devices><Device>
                <Type ProductCode="#x1" RevisionNo="#x1">SimpleIO</Type>
                <Name>Simple IO Terminal</Name>
                <Sm StartAddress="#x1000" ControlByte="#x64" Enable="1">Outputs</Sm>
            </Device></Devices></Descriptions>
        </EtherCATInfo>)";

    ESI::Parser parser;
    ESI::Device device = parser.loadDeviceString(xml);
    ASSERT_EQ(device.sync_managers.size(), 1u);
    // Synthesised 0x1C00 from the single <Sm> still appears
    auto [obj, _] = findObject(device.dictionary, 0x1C00, 0);
    ASSERT_NE(obj, nullptr);
    ASSERT_EQ(obj->entries.size(), 2u);  // size + 1 SM
}

TEST(ESIParser, CoE_alias_is_backwards_compatible)
{
    // The CoE::EsiParser alias must still resolve to ESI::Parser.
    CoE::EsiParser parser;
    auto dictionary = parser.loadFile("kickcat_esi_test_basic.xml");
    ASSERT_EQ(dictionary.size(), 9u);
    ASSERT_STREQ(parser.vendor(), "KickCAT");
}

TEST(ESIParser, buildMappingObject_throws_when_more_than_255_entries)
{
    ESI::Pdo pdo;
    pdo.index = 0x1600;
    pdo.name  = "Too many entries";
    pdo.entries.resize(256);  // SubIndex space is a single byte

    try
    {
        (void) ESI::Parser::buildMappingObject(pdo, true);
        FAIL() << "expected invalid_argument";
    }
    catch (std::invalid_argument const& e)
    {
        std::string msg = e.what();
        ASSERT_NE(msg.find("255 entries"), std::string::npos) << msg;
    }
}

TEST(ESIParser, buildAssignmentObject_throws_when_more_than_255_pdos)
{
    std::vector<ESI::Pdo> pdos(256);
    for (std::size_t i = 0; i < pdos.size(); ++i)
    {
        pdos[i].index = static_cast<uint16_t>(0x1600 + i);
    }

    try
    {
        (void) ESI::Parser::buildAssignmentObject(pdos, 0x1C12, true);
        FAIL() << "expected invalid_argument";
    }
    catch (std::invalid_argument const& e)
    {
        std::string msg = e.what();
        ASSERT_NE(msg.find("255 PDOs"), std::string::npos) << msg;
    }
}

TEST(ESIParser, throws_on_eeprom_category_without_payload)
{
    char const* xml = R"(<?xml version="1.0"?>
        <EtherCATInfo>
            <Vendor><Id>#x1</Id><Name>V</Name></Vendor>
            <Descriptions><Devices><Device>
                <Type ProductCode="#x1" RevisionNo="#x1">T</Type>
                <Eeprom>
                    <ByteSize>2048</ByteSize>
                    <ConfigData>05060708</ConfigData>
                    <Category><CatNo>30</CatNo></Category>
                </Eeprom>
            </Device></Devices></Descriptions>
        </EtherCATInfo>)";

    ESI::Parser parser;
    try
    {
        (void) parser.loadString(xml);
        FAIL() << "expected invalid_argument";
    }
    catch (std::invalid_argument const& e)
    {
        std::string msg = e.what();
        ASSERT_NE(msg.find("missing payload"), std::string::npos) << msg;
    }
}

TEST(ESIParser, eeprom_category_empty_datastring_is_empty_string)
{
    // <DataString/> is a valid (empty) xs:string per the schema: payload is
    // selected by element presence, so the category parses with an empty value.
    char const* xml = R"(<?xml version="1.0"?>
        <EtherCATInfo>
            <Vendor><Id>#x1</Id><Name>V</Name></Vendor>
            <Descriptions><Devices><Device>
                <Type ProductCode="#x1" RevisionNo="#x1">T</Type>
                <Eeprom>
                    <ByteSize>2048</ByteSize>
                    <ConfigData>05060708</ConfigData>
                    <Category><CatNo>30</CatNo><DataString></DataString></Category>
                </Eeprom>
            </Device></Devices></Descriptions>
        </EtherCATInfo>)";

    ESI::Parser parser;
    ESI::Device device = parser.loadDeviceString(xml);
    ASSERT_TRUE(device.eeprom.has_value());
    ASSERT_EQ(device.eeprom->categories.size(), 1u);
    ASSERT_TRUE(device.eeprom->categories[0].data_string.has_value());
    ASSERT_TRUE(device.eeprom->categories[0].data_string->empty());
}

TEST(ESIParser, throws_on_eeprom_category_empty_datauint)
{
    // A numeric payload still requires a value: empty <DataUINT/> must throw.
    char const* xml = R"(<?xml version="1.0"?>
        <EtherCATInfo>
            <Vendor><Id>#x1</Id><Name>V</Name></Vendor>
            <Descriptions><Devices><Device>
                <Type ProductCode="#x1" RevisionNo="#x1">T</Type>
                <Eeprom>
                    <ByteSize>2048</ByteSize>
                    <ConfigData>05060708</ConfigData>
                    <Category><CatNo>30</CatNo><DataUINT></DataUINT></Category>
                </Eeprom>
            </Device></Devices></Descriptions>
        </EtherCATInfo>)";

    ESI::Parser parser;
    ASSERT_THROW((void) parser.loadDeviceString(xml), std::invalid_argument);
}

TEST(ESIParser, throws_on_dc_opmode_missing_mandatory_fields)
{
    char const* missing_name = R"(<?xml version="1.0"?>
        <EtherCATInfo>
            <Vendor><Id>#x1</Id><Name>V</Name></Vendor>
            <Descriptions><Devices><Device>
                <Type ProductCode="#x1" RevisionNo="#x1">T</Type>
                <Dc><OpMode><AssignActivate>#x300</AssignActivate></OpMode></Dc>
            </Device></Devices></Descriptions>
        </EtherCATInfo>)";

    char const* missing_assign_activate = R"(<?xml version="1.0"?>
        <EtherCATInfo>
            <Vendor><Id>#x1</Id><Name>V</Name></Vendor>
            <Descriptions><Devices><Device>
                <Type ProductCode="#x1" RevisionNo="#x1">T</Type>
                <Dc><OpMode><Name>Sync</Name></OpMode></Dc>
            </Device></Devices></Descriptions>
        </EtherCATInfo>)";

    char const* sm_missing_no = R"(<?xml version="1.0"?>
        <EtherCATInfo>
            <Vendor><Id>#x1</Id><Name>V</Name></Vendor>
            <Descriptions><Devices><Device>
                <Type ProductCode="#x1" RevisionNo="#x1">T</Type>
                <Dc><OpMode>
                    <Name>Sync</Name><AssignActivate>#x300</AssignActivate>
                    <Sm><Pdo OSFac="2">#x1A00</Pdo></Sm>
                </OpMode></Dc>
            </Device></Devices></Descriptions>
        </EtherCATInfo>)";

    ESI::Parser parser;
    ASSERT_THROW((void) parser.loadString(missing_name),            std::invalid_argument);
    ASSERT_THROW((void) parser.loadString(missing_assign_activate), std::invalid_argument);

    try
    {
        (void) parser.loadString(sm_missing_no);
        FAIL() << "expected invalid_argument";
    }
    catch (std::invalid_argument const& e)
    {
        std::string msg = e.what();
        ASSERT_NE(msg.find("@No"), std::string::npos) << msg;
    }
}
