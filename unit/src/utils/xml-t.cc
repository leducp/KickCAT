#include <gtest/gtest.h>
#include <stdexcept>

#include "kickcat/utils/xml.h"

using namespace kickcat;
using namespace tinyxml2;

namespace
{
    class XML : public ::testing::Test
    {
    protected:
        XMLElement* load(char const* text)
        {
            EXPECT_EQ(doc_.Parse(text), XML_SUCCESS);
            return doc_.RootElement();
        }

        template<typename F>
        std::string errorOf(F parse)
        {
            try
            {
                parse();
            }
            catch (std::invalid_argument const& e)
            {
                return e.what();
            }
            ADD_FAILURE() << "expected std::invalid_argument";
            return {};
        }

        XMLDocument doc_;
    };
}

TEST_F(XML, path_counts_same_name_siblings_only)
{
    XMLElement* root = load("<A><B/><C><D/></C><C><D/><E/></C></A>");
    XMLElement* d = root->FirstChildElement("C")->NextSiblingElement("C")->FirstChildElement("D");
    EXPECT_EQ(xml::pathOf(d), "A/C[1]/D");
    EXPECT_EQ(xml::pathOf(root->FirstChildElement("B")), "A/B");
    EXPECT_EQ(xml::pathOf(root->FirstChildElement("C")), "A/C[0]");
    EXPECT_EQ(errorOf([&]() { xml::fail(d, "boom"); }), "boom in A/C[1]/D");
}

TEST_F(XML, text)
{
    XMLElement* root = load("<A><B> 12<!-- split -->3<![CDATA[4]]>\n</B><E>  </E></A>");
    EXPECT_EQ(xml::textOf(root->FirstChildElement("B")), "1234");
    EXPECT_EQ(xml::optionalText(root, "B"), "1234");
    EXPECT_EQ(xml::optionalText(root, "C"), "");
    EXPECT_EQ(xml::requireText(root, "B"), "1234");
    EXPECT_EQ(errorOf([&]() { xml::requireText(root, "C"); }), "missing mandatory <C> in A");
    EXPECT_EQ(errorOf([&]() { xml::requireText(root, "E"); }), "empty <E> in A/E");
}

TEST_F(XML, attributes)
{
    XMLElement* root = load("<A N=\" #x10 \" B=\" true\" Bad=\"yes\" Big=\"256\"/>");
    EXPECT_EQ(xml::attributeOf(root, "N"), "#x10");
    EXPECT_FALSE(xml::attributeOf(root, "M").has_value());
    EXPECT_EQ(xml::numberAttribute<uint8_t>(root, "N"), 16);
    EXPECT_FALSE(xml::numberAttribute<uint8_t>(root, "M").has_value());
    EXPECT_TRUE(xml::boolAttribute(root, "B"));
    EXPECT_FALSE(xml::boolAttribute(root, "M"));
    EXPECT_EQ(errorOf([&]() { xml::boolAttribute(root, "Bad"); }), "@Bad: invalid xs:boolean 'yes' in A");
    EXPECT_EQ(errorOf([&]() { xml::numberAttribute<uint8_t>(root, "Big"); }), "@Big: value 256 out of range [0, 255] in A");
}

TEST_F(XML, numbers)
{
    XMLElement* root = load("<A><P>-1</P><Q>#xFFFF</Q><R>abc</R></A>");
    EXPECT_EQ(xml::requireNumber<int16_t>(root, "Q", xml::BIT_PATTERN), -1);
    EXPECT_EQ(xml::requireNumber<uint16_t>(root, "P", xml::BIT_PATTERN), 0xFFFF);
    EXPECT_EQ(xml::optionalNumber<uint16_t>(root, "Q"), 0xFFFF);
    EXPECT_FALSE(xml::optionalNumber<uint16_t>(root, "S").has_value());
    EXPECT_EQ(errorOf([&]() { xml::requireNumber<uint16_t>(root, "P"); }), "value -1 out of range [0, 65535] in A/P");
    EXPECT_EQ(errorOf([&]() { xml::requireNumber<int16_t>(root, "Q"); }), "value 65535 out of range [-32768, 32767] in A/Q");
    EXPECT_EQ(errorOf([&]() { xml::requireNumber<uint16_t>(root, "R"); }), "invalid numeric value 'abc' in A/R");
    EXPECT_EQ(errorOf([&]() { xml::requireNumber<uint16_t>(root, "S"); }), "missing mandatory <S> in A");
}

TEST_F(XML, booleans_and_binary)
{
    XMLElement* root = load("<A><T>1</T><F>false</F><H>01 ff</H><Odd>0</Odd></A>");
    EXPECT_TRUE(xml::optionalBool(root, "T"));
    EXPECT_FALSE(xml::optionalBool(root, "F"));
    EXPECT_FALSE(xml::optionalBool(root, "M"));
    EXPECT_EQ(xml::hexBinary(root->FirstChildElement("H")), (std::vector<uint8_t>{0x01, 0xff}));
    EXPECT_NE(errorOf([&]() { xml::hexBinary(root->FirstChildElement("Odd")); }).find("odd length"), std::string::npos);
}

TEST_F(XML, all_and_context)
{
    XMLElement* root = load("<A><I>1</I><J/><I>2</I></A>");
    auto values = xml::all(root, "I", [](XMLElement* e) { return xml::numberOf<uint8_t>(e); });
    EXPECT_EQ(values, (std::vector<uint8_t>{1, 2}));

    EXPECT_EQ(xml::withContext("ESI", []() { return 3; }), 3);
    EXPECT_EQ(errorOf([&]() { xml::withContext("ESI", [&]() { xml::fail(root, "boom"); }); }), "ESI: boom in A");
}
