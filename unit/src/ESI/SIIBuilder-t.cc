#include <gtest/gtest.h>

#include "kickcat/ESI/SIIBuilder.h"
#include "kickcat/ESI/Parser.h"
#include "kickcat/SIIParser.h"

using namespace kickcat;

namespace
{
    // Minimal Input terminal (no mailbox): one Input SM, one Inputs FMMU, one
    // TxPDO with a BOOL entry. Mirrors an EL1xxx-style digital input.
    ESI::Device makeInputDevice()
    {
        ESI::Device d;
        d.type          = "EL0001";
        d.name          = "Test input";
        d.vendor_id     = 0x2;
        d.product_code  = 0x1234;
        d.revision_no   = 0x10;
        d.serial_no     = 0;

        ESI::SmInfo sm;
        sm.type          = SyncManager::Input;
        sm.start_address = 0x1000;
        sm.default_size  = 1;
        sm.control_byte  = 0x00;
        sm.enable        = 1;
        d.sync_managers.push_back(sm);

        ESI::Fmmu fmmu;
        fmmu.type = fmmu::Inputs;
        d.fmmus.push_back(fmmu);

        ESI::Pdo pdo;
        pdo.index = 0x1A00;
        pdo.name  = "Channel 1";
        pdo.sm    = 0;
        ESI::PdoEntry e;
        e.index = 0x6000; e.subindex = 1; e.bit_len = 1; e.name = "Input"; e.data_type = "BOOL";
        pdo.entries.push_back(e);
        d.tx_pdos.push_back(pdo);

        ESI::Eeprom eep;
        eep.byte_size   = 2048;
        eep.config_data = {0x04, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};  // 8 bytes (EL1008-like)
        d.eeprom        = eep;
        return d;
    }
}

TEST(SIIBuilder, maps_parsed_device_to_sii_struct)
{
    ESI::Parser parser;
    ESI::Device dev = parser.loadDevice("kickcat_esi_test_sm_fmmu.xml");
    eeprom::SII sii = ESI::buildSII(dev);

    // Identity
    ASSERT_EQ(sii.info.vendor_id,       0x0CAFEu);
    ASSERT_EQ(sii.info.product_code,    0x00005555u);
    ASSERT_EQ(sii.info.revision_number, 0x1u);
    ASSERT_EQ(sii.info.size,            15u);  // 2048 / 128 - 1
    ASSERT_EQ(sii.info.version,         1u);

    // Mailbox words derived from the mailbox SyncManagers
    ASSERT_EQ(sii.info.standard_recv_mbx_offset, 0x1000u);
    ASSERT_EQ(sii.info.standard_recv_mbx_size,   128u);
    ASSERT_EQ(sii.info.standard_send_mbx_offset, 0x1400u);
    ASSERT_EQ(sii.info.standard_send_mbx_size,   128u);
    // Bootstrap (0010800010108000 -> recv off/size, send off/size)
    ASSERT_EQ(sii.info.bootstrap_recv_mbx_offset, 0x1000u);
    ASSERT_EQ(sii.info.bootstrap_recv_mbx_size,   0x0080u);
    ASSERT_EQ(sii.info.bootstrap_send_mbx_offset, 0x1010u);
    ASSERT_EQ(sii.info.bootstrap_send_mbx_size,   0x0080u);
    // CoE/EoE/AoE/FoE/SoE/VoE present in the fixture (VoE bit per ETG.2010 Table 4 word 0x1C)
    ASSERT_EQ(sii.info.mailbox_protocol,
              eeprom::MailboxProtocol::AoE | eeprom::MailboxProtocol::EoE | eeprom::MailboxProtocol::CoE
            | eeprom::MailboxProtocol::FoE | eeprom::MailboxProtocol::SoE | eeprom::MailboxProtocol::VoE);

    // SyncManagers
    ASSERT_EQ(sii.syncManagers.size(), 4u);
    ASSERT_EQ(sii.syncManagers[0].type,            SyncManager::MailboxOut);
    ASSERT_EQ(sii.syncManagers[1].type,            SyncManager::MailboxIn);
    ASSERT_EQ(sii.syncManagers[2].type,            SyncManager::Output);
    ASSERT_EQ(sii.syncManagers[2].start_address,   0x1800u);
    ASSERT_EQ(sii.syncManagers[2].length,          40u);
    ASSERT_EQ(sii.syncManagers[2].control_register, 0x64u);
    ASSERT_EQ(sii.syncManagers[2].enable,          1u);
    ASSERT_EQ(sii.syncManagers[3].type,            SyncManager::Input);

    // FMMUs
    ASSERT_EQ(sii.fmmus.size(), 3u);
    ASSERT_EQ(sii.fmmus[0], fmmu::Outputs);
    ASSERT_EQ(sii.fmmus[1], fmmu::Inputs);
    ASSERT_EQ(sii.fmmus[2], fmmu::MBoxState);

    // General CoE detail bits
    ASSERT_EQ(sii.general.SDO_set,             1u);
    ASSERT_EQ(sii.general.SDO_info,            1u);
    ASSERT_EQ(sii.general.PDO_assign,          1u);
    ASSERT_EQ(sii.general.PDO_configuration,   1u);
    ASSERT_EQ(sii.general.SDO_complete_access, 1u);
    ASSERT_EQ(sii.general.PDO_upload,          0u);
    ASSERT_EQ(sii.general.FoE_details,         1u);
    ASSERT_EQ(sii.general.EoE_details,         1u);
    ASSERT_EQ(sii.general.SoE_channels,        1u);
    ASSERT_EQ(sii.strings.at(sii.general.device_name_id), "KickCAT ESI Test Sm/Fmmu/Su");

    // PDOs
    ASSERT_EQ(sii.RxPDO.size(), 1u);
    ASSERT_EQ(sii.RxPDO[0].index,        0x1600u);
    ASSERT_EQ(sii.RxPDO[0].sync_manager, 2u);
    ASSERT_EQ(sii.strings.at(sii.RxPDO[0].name_index), "Outputs");
    ASSERT_EQ(sii.RxPDO[0].entries.at(0).data_type, static_cast<uint8_t>(CoE::DataType::UNSIGNED16));

    ASSERT_EQ(sii.TxPDO.size(), 1u);
    ASSERT_EQ(sii.TxPDO[0].index, 0x1A00u);
    ASSERT_EQ(sii.TxPDO[0].entries.size(), 2u);
    ASSERT_EQ(sii.strings.at(sii.TxPDO[0].entries.at(1).name), "Status");
    ASSERT_EQ(sii.TxPDO[0].entries.at(0).data_type, static_cast<uint8_t>(CoE::DataType::UNSIGNED16));
    ASSERT_EQ(sii.TxPDO[0].entries.at(1).data_type, static_cast<uint8_t>(CoE::DataType::UNSIGNED8));
    ASSERT_EQ(sii.TxPDO[0].entries.at(1).bitlen, 8u);

    // Vendor categories preserved (fixture uses CatNo 30/40 as opaque values here)
    ASSERT_EQ(sii.unknownCategories.size(), 2u);
}

TEST(SIIBuilder, synthesizes_dc_category)
{
    ESI::Parser parser;
    ESI::Device dev = parser.loadDevice("kickcat_esi_test_sm_fmmu.xml");
    eeprom::SII sii = ESI::buildSII(dev);

    // Fixture has 2 OpModes (Synchron, FreeRun); ETG.2010 Table 12 = 24 bytes each.
    ASSERT_EQ(sii.dc.size(), 2u * 24u);

    auto rd32 = [&](std::size_t o)
    {
        return static_cast<uint32_t>(sii.dc[o] | (sii.dc[o + 1] << 8) | (sii.dc[o + 2] << 16)
                                     | (static_cast<uint32_t>(sii.dc[o + 3]) << 24));
    };
    auto rd16 = [&](std::size_t o) { return static_cast<uint16_t>(sii.dc[o] | (sii.dc[o + 1] << 8)); };

    // Synchron: CycleTimeSync0=1000000 (Factor 1), ShiftTimeSync0=100, AssignActivate=0x300
    ASSERT_EQ(rd32(0x00), 1000000u);                  // cycleTime0
    ASSERT_EQ(rd32(0x04), 100u);                      // shiftTime0
    ASSERT_EQ(rd16(0x0E), 0x300u);                    // assignActivate
    ASSERT_EQ(rd16(0x10), 1u);                        // sync0CycleFactor
    ASSERT_EQ(sii.strings.at(sii.dc[0x12]), "Synchron");   // nameIdx
    ASSERT_EQ(sii.strings.at(sii.dc[0x13]), "SM-Synchron");// descIdx

    // FreeRun (second element): AssignActivate=0
    ASSERT_EQ(rd16(0x18 + 0x0E), 0u);
}

TEST(SIIBuilder, round_trips_through_serialize_and_parse)
{
    ESI::Device dev = makeInputDevice();
    auto bytes = ESI::buildSII(dev).serialize();

    eeprom::SII parsed;
    parsed.parse(bytes.data(), bytes.size());

    ASSERT_EQ(parsed.info.vendor_id,       0x2u);
    ASSERT_EQ(parsed.info.product_code,    0x1234u);
    ASSERT_EQ(parsed.info.revision_number, 0x10u);
    ASSERT_EQ(parsed.syncManagers.size(),  1u);
    ASSERT_EQ(parsed.syncManagers[0].type, SyncManager::Input);
    ASSERT_FALSE(parsed.fmmus.empty());        // serialize pads an odd count with a trailing 0 byte
    ASSERT_EQ(parsed.fmmus[0],             fmmu::Inputs);
    ASSERT_EQ(parsed.TxPDO.size(),         1u);
    ASSERT_EQ(parsed.TxPDO[0].index,       0x1A00u);
    ASSERT_EQ(parsed.TxPDO[0].entries.at(0).data_type, static_cast<uint8_t>(CoE::DataType::BOOLEAN));
    ASSERT_EQ(parsed.strings.at(parsed.TxPDO[0].entries.at(0).name), "Input");

    // No mailbox -> all mailbox words and the protocol mask are zero.
    ASSERT_EQ(parsed.info.mailbox_protocol,          0u);
    ASSERT_EQ(parsed.info.standard_recv_mbx_offset,  0u);
    ASSERT_EQ(parsed.info.standard_send_mbx_offset,  0u);

    // serialize() recomputed the InfoEntry CRC; it must validate after parse.
    ASSERT_EQ(eeprom::computeInfoCRC(parsed.info), parsed.info.crc);
}

TEST(SIIBuilder, serialize_clamps_string_length_to_one_byte)
{
    eeprom::SII sii;
    sii.strings.push_back("");                     // reserved index 0
    sii.strings.push_back(std::string(300, 'a'));  // would wrap the 1-byte length prefix
    sii.strings.push_back("next");
    auto bytes = sii.serialize();

    eeprom::SII parsed;
    parsed.parse(bytes.data(), bytes.size());
    ASSERT_EQ(3u, parsed.strings.size());
    ASSERT_EQ(std::string(255, 'a'), parsed.strings[1]);
    ASSERT_EQ("next", parsed.strings[2]);
}

TEST(SIIBuilder, maps_pdo_entry_data_types_and_padding)
{
    ESI::Device dev = makeInputDevice();
    dev.tx_pdos.clear();

    ESI::Pdo pdo;
    pdo.index = 0x1A01;
    pdo.name  = "Mixed";
    pdo.sm    = 0;
    auto add = [&](uint16_t index, uint8_t sub, uint16_t bits, std::string name, std::string type)
    {
        ESI::PdoEntry e;
        e.index = index; e.subindex = sub; e.bit_len = bits; e.name = std::move(name); e.data_type = std::move(type);
        pdo.entries.push_back(e);
    };
    add(0x7000, 1, 1,  "b",   "BOOL");
    add(0x7000, 2, 16, "u16", "UINT");
    add(0x7000, 3, 32, "u32", "UDINT");
    add(0,      0, 4,  "",    "");      // padding gap
    dev.tx_pdos.push_back(pdo);

    eeprom::SII sii = ESI::buildSII(dev);
    auto const& entries = sii.TxPDO.at(0).entries;
    ASSERT_EQ(entries.at(0).data_type, static_cast<uint8_t>(CoE::DataType::BOOLEAN));
    ASSERT_EQ(entries.at(1).data_type, static_cast<uint8_t>(CoE::DataType::UNSIGNED16));
    ASSERT_EQ(entries.at(2).data_type, static_cast<uint8_t>(CoE::DataType::UNSIGNED32));
    ASSERT_EQ(entries.at(3).data_type, 0u);   // padding: no type
    ASSERT_EQ(entries.at(3).name,      0u);   // padding: no name
}

TEST(SIIBuilder, parse_reads_all_pdos_of_one_category)
{
    // ETG.2010 Table 14: a TxPDO/RxPDO category holds one block per PDO,
    // back to back (was: only the first PDO of the category was read).
    std::vector<uint8_t> img(eeprom::START_CATEGORY * 2, 0);
    auto w16 = [&](uint16_t v)
    {
        img.push_back(static_cast<uint8_t>(v & 0xFF));
        img.push_back(static_cast<uint8_t>(v >> 8));
    };
    auto pdo = [&](uint16_t index, uint8_t name_idx, uint16_t entry_index)
    {
        w16(index);
        img.push_back(1);         // nb entries
        img.push_back(3);         // sync manager
        img.push_back(0);         // synchronization
        img.push_back(name_idx);
        w16(0);                   // flags
        w16(entry_index);
        img.push_back(1);         // subindex
        img.push_back(0);         // name
        img.push_back(0x01);      // data type (BOOLEAN)
        img.push_back(1);         // bitlen
        w16(0);                   // entry flags
    };

    w16(eeprom::Category::TxPDO);
    w16(16);                      // 2 * (8-byte header + one 8-byte entry), in words
    pdo(0x1A00, 2, 0x6000);
    pdo(0x1A01, 3, 0x6010);
    w16(eeprom::Category::End);
    w16(0);

    eeprom::SII sii;
    sii.parse(img.data(), img.size());
    ASSERT_EQ(sii.TxPDO.size(), 2u);
    ASSERT_EQ(sii.TxPDO[0].index, 0x1A00u);
    ASSERT_EQ(sii.TxPDO[0].entries.size(), 1u);
    ASSERT_EQ(sii.TxPDO[0].entries[0].index, 0x6000u);
    ASSERT_EQ(sii.TxPDO[1].index, 0x1A01u);
    ASSERT_EQ(sii.TxPDO[1].entries.size(), 1u);
    ASSERT_EQ(sii.TxPDO[1].entries[0].index, 0x6010u);
}

TEST(SIIBuilder, sii_registerString_dedups_and_is_one_based)
{
    eeprom::SII sii;
    ASSERT_EQ(sii.registerString(""), 0);            // empty -> no string
    uint8_t a = sii.registerString("alpha");
    uint8_t b = sii.registerString("beta");
    ASSERT_EQ(a, 1);
    ASSERT_EQ(b, 2);
    ASSERT_EQ(sii.registerString("alpha"), a);       // deduplicated
    ASSERT_EQ(sii.getString(a), "alpha");          // round-trips with getString
    ASSERT_EQ(sii.getString(b), "beta");
}

TEST(SIIBuilder, getString_resolves_one_based_index)
{
    eeprom::SII sii;
    sii.strings = {"", "first", "second"};  // strings[0] reserved; SII index i -> strings[i]
    ASSERT_EQ(sii.getString(0), "");        // 0 means "no string"
    ASSERT_EQ(sii.getString(1), "first");
    ASSERT_EQ(sii.getString(2), "second");
    ASSERT_EQ(sii.getString(3), "");        // out of range
}

TEST(SIIBuilder, raw_eeprom_image_is_returned_verbatim)
{
    ESI::Device dev = makeInputDevice();
    dev.eeprom->raw_data = {0xDE, 0xAD, 0xBE, 0xEF};
    auto image = ESI::buildEepromImage(dev);
    ASSERT_EQ(image, (std::vector<uint8_t>{0xDE, 0xAD, 0xBE, 0xEF}));
}
