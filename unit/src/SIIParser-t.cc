// Malformed / truncated SII images: real partially-programmed EEPROMs declare
// categories whose payload is shorter than the fixed structure they carry.
#include <gtest/gtest.h>

#include "kickcat/Error.h"
#include "kickcat/SIIParser.h"

using namespace kickcat;

namespace
{
    class Image
    {
    public:
        Image()
            : bytes_(eeprom::START_CATEGORY * 2, 0)
        {
        }

        void w8(uint8_t v)
        {
            bytes_.push_back(v);
        }

        void w16(uint16_t v)
        {
            bytes_.push_back(static_cast<uint8_t>(v & 0xFF));
            bytes_.push_back(static_cast<uint8_t>(v >> 8));
        }

        void category(uint16_t type, std::vector<uint8_t> const& payload)
        {
            w16(type);
            w16(static_cast<uint16_t>(payload.size() / 2));
            bytes_.insert(bytes_.end(), payload.begin(), payload.end());
        }

        void end()
        {
            w16(eeprom::Category::End);
            w16(0);
        }

        eeprom::SII parse()
        {
            eeprom::SII sii;
            sii.parse(bytes_.data(), bytes_.size());
            return sii;
        }

    private:
        std::vector<uint8_t> bytes_;
    };
}

TEST(SIIParser, truncated_general_category_is_zero_filled)
{
    // ETG.2010 general category is 32 bytes; declare only 4: the parser shall not
    // read past the declared payload and the missing fields shall stay zero.
    Image img;
    img.category(eeprom::Category::General, {2, 3, 4, 5});
    // The next category starts right after the 4 declared bytes: if the parser
    // memcpys the full GeneralEntry it swallows these (non-zero) bytes instead.
    img.category(eeprom::Category::FMMU, {0xAA, 0xBB});
    img.end();

    eeprom::SII sii = img.parse();
    ASSERT_EQ(2, sii.general.group_info_id);
    ASSERT_EQ(3, sii.general.image_name_id);
    ASSERT_EQ(4, sii.general.device_order_id);
    ASSERT_EQ(5, sii.general.device_name_id);
    ASSERT_EQ(0, sii.general.FoE_details);
    ASSERT_EQ(0, sii.general.EoE_details);
    ASSERT_EQ(0, sii.general.current_on_ebus);
    ASSERT_EQ(0, sii.general.physical_memory_address);

    ASSERT_EQ(2u, sii.fmmus.size());
    ASSERT_EQ(0xAA, sii.fmmus[0]);
    ASSERT_EQ(0xBB, sii.fmmus[1]);
}

TEST(SIIParser, category_bigger_than_image_stops_parsing)
{
    Image img;
    img.w16(eeprom::Category::Strings);
    img.w16(0xFFFF);  // claims 128 KiB of payload: way past the image end
    img.w16(0);

    eeprom::SII sii = img.parse();
    ASSERT_TRUE(sii.strings.empty());  // category dropped before reaching the handler
}

TEST(SIIParser, truncated_string_entry_is_dropped)
{
    Image img;
    img.category(eeprom::Category::Strings, {
        2,              // 2 strings announced
        3, 'a', 'b', 'c',
        200, 'x', 0,    // length runs past the category payload (+1 pad byte)
    });
    img.end();

    eeprom::SII sii = img.parse();
    ASSERT_EQ(2u, sii.strings.size());
    ASSERT_EQ("abc", sii.strings[1]);
}

TEST(SIIParser, truncated_sync_manager_entry_is_dropped)
{
    Image img;
    img.category(eeprom::Category::SyncM, {
        0x00, 0x10, 0x80, 0x00, 0x26, 0x00, 0x01, 0x01,  // one full 8-byte entry
        0x00, 0x14, 0x80, 0x00,                          // half an entry
    });
    img.end();

    eeprom::SII sii = img.parse();
    ASSERT_EQ(1u, sii.syncManagers.size());
    ASSERT_EQ(0x1000, sii.syncManagers[0].start_address);
    ASSERT_EQ(0x0080, sii.syncManagers[0].length);
}

TEST(SIIParser, truncated_pdo_entry_list_is_dropped)
{
    Image img;
    img.category(eeprom::Category::TxPDO, {
        0x00, 0x1A,     // index 0x1A00
        2,              // announces 2 entries
        3, 0, 0,        // sm, synchro, name
        0x00, 0x00,     // flags
        0x00, 0x60, 1, 0, 0x01, 1, 0x00, 0x00,  // first 8-byte entry only
    });
    img.end();

    eeprom::SII sii = img.parse();
    ASSERT_EQ(1u, sii.TxPDO.size());
    ASSERT_EQ(0x1A00u, sii.TxPDO[0].index);
    ASSERT_EQ(1u, sii.TxPDO[0].entries.size());
    ASSERT_EQ(0x6000u, sii.TxPDO[0].entries[0].index);
}

TEST(SIIParser, image_too_small_for_info_throws)
{
    uint8_t bytes[4] = {0, 0, 0, 0};
    eeprom::SII sii;
    ASSERT_THROW(sii.parse(bytes, sizeof(bytes)), Error);
}
