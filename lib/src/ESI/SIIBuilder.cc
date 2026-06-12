#include "kickcat/ESI/SIIBuilder.h"

#include <algorithm>
#include <array>
#include <cstring>

#include "kickcat/debug.h"
#include "kickcat/ESI/Parser.h"

namespace kickcat::ESI
{
namespace
{
    // buildMappings stays here (not an SII method): it consumes ESI::Pdo, and
    // eeprom::SII must not depend on the ESI layer.
    void buildMappings(eeprom::SII& sii, std::vector<Pdo> const& pdos, std::vector<eeprom::PDOMapping>& out)
    {
        for (auto const& pdo : pdos)
        {
            eeprom::PDOMapping mapping{};
            mapping.index = pdo.index;
            if (pdo.sm)
            {
                mapping.sync_manager = static_cast<uint8_t>(*pdo.sm);
            }
            else
            {
                mapping.sync_manager = 0xFF;  // unassigned
            }
            mapping.synchronization = 0;
            mapping.name_index      = sii.registerString(pdo.name);
            mapping.flags           = 0;

            for (auto const& entry : pdo.entries)
            {
                eeprom::PDOEntry pe{};
                pe.index     = entry.index;
                pe.subindex  = entry.subindex;
                pe.name      = sii.registerString(entry.name);
                pe.data_type = 0;
                if (entry.index != 0)  // index 0 is a padding gap: no type/name
                {
                    auto type = Parser::coeTypeFromLabel(entry.data_type);
                    if (type)
                    {
                        pe.data_type = static_cast<uint8_t>(*type);
                    }
                }
                if (entry.bit_len > 0xFF)
                {
                    esi_warning("SII: PDO 0x%04x entry bit_len %u > 255, truncated\n", pdo.index, entry.bit_len);
                }
                pe.bitlen = static_cast<uint8_t>(entry.bit_len);
                pe.flags  = 0;
                mapping.entries.push_back(pe);
            }
            out.push_back(std::move(mapping));
        }
    }
}

eeprom::SII buildSII(Device const& device)
{
    eeprom::SII sii;
    sii.strings.push_back(std::string{});  // index 0 is the reserved empty string

    // ---- Info area (first 16 bytes) ----
    if (device.eeprom and not device.eeprom->config_data.empty())
    {
        // config_data holds words 0..6 (PDI control/config, alias, ...). Word 7
        // is the CRC, recomputed by SII::serialize(), so never copy past byte 14.
        std::size_t n = std::min<std::size_t>(device.eeprom->config_data.size(), 14);
        std::memcpy(&sii.info, device.eeprom->config_data.data(), n);
    }
    sii.info.crc = 0;

    sii.info.vendor_id       = device.vendor_id;
    sii.info.product_code    = device.product_code;
    sii.info.revision_number = device.revision_no;
    sii.info.serial_number   = device.serial_no;

    for (auto const& sm : device.sync_managers)
    {
        if (sm.type == SyncManager::MailboxOut)
        {
            sii.info.standard_recv_mbx_offset = sm.start_address;
            sii.info.standard_recv_mbx_size   = sm.default_size;
        }
        else if (sm.type == SyncManager::MailboxIn)
        {
            sii.info.standard_send_mbx_offset = sm.start_address;
            sii.info.standard_send_mbx_size   = sm.default_size;
        }
    }

    if (device.eeprom and device.eeprom->bootstrap.size() >= 8)
    {
        auto const& b = device.eeprom->bootstrap;
        sii.info.bootstrap_recv_mbx_offset = static_cast<uint16_t>(b[0] | (b[1] << 8));
        sii.info.bootstrap_recv_mbx_size   = static_cast<uint16_t>(b[2] | (b[3] << 8));
        sii.info.bootstrap_send_mbx_offset = static_cast<uint16_t>(b[4] | (b[5] << 8));
        sii.info.bootstrap_send_mbx_size   = static_cast<uint16_t>(b[6] | (b[7] << 8));
    }

    if (device.mailbox)
    {
        auto const& mb = *device.mailbox;
        uint16_t protocol = 0;
        if (mb.aoe) { protocol |= eeprom::MailboxProtocol::AoE; }
        if (mb.eoe) { protocol |= eeprom::MailboxProtocol::EoE; }
        if (mb.coe) { protocol |= eeprom::MailboxProtocol::CoE; }
        if (mb.foe) { protocol |= eeprom::MailboxProtocol::FoE; }
        if (mb.soe) { protocol |= eeprom::MailboxProtocol::SoE; }
        if (mb.voe) { protocol |= eeprom::MailboxProtocol::VoE; }
        sii.info.mailbox_protocol = protocol;
    }

    uint16_t size_word = 0x000F;  // default 2 KiB when byte_size is unknown
    if (device.eeprom and device.eeprom->byte_size and (*device.eeprom->byte_size >= 128))
    {
        size_word = static_cast<uint16_t>(*device.eeprom->byte_size / 128 - 1);
    }
    sii.info.size    = size_word;
    sii.info.version = 1;

    // ---- General + strings ----
    std::string device_name = device.name;
    if (device_name.empty())
    {
        device_name = device.type;
    }
    sii.general.device_name_id    = sii.registerString(device_name);
    sii.general.group_info_id     = sii.registerString(device.group_type);
    sii.general.group_info_id_dup = sii.general.group_info_id;
    sii.general.device_order_id   = sii.registerString(device.type);  // order code not surfaced; type stands in

    if (device.mailbox)
    {
        auto const& mb = *device.mailbox;
        if (mb.coe)
        {
            sii.general.SDO_set             = 1;
            sii.general.SDO_info            = mb.coe->sdo_info;
            sii.general.PDO_assign          = mb.coe->pdo_assign;
            sii.general.PDO_configuration   = mb.coe->pdo_config;
            sii.general.PDO_upload          = mb.coe->pdo_upload;
            sii.general.SDO_complete_access = mb.coe->complete_access;
        }
        if (mb.foe) { sii.general.FoE_details  = 1; }
        if (mb.eoe) { sii.general.EoE_details  = 1; }
        if (mb.soe) { sii.general.SoE_channels = 1; }
    }

    // ---- FMMU ----
    for (auto const& fmmu : device.fmmus)
    {
        sii.fmmus.push_back(static_cast<uint8_t>(fmmu.type));
    }

    // ---- SyncManager ----
    for (auto const& sm : device.sync_managers)
    {
        eeprom::SyncManagerEntry entry{};
        entry.start_address    = sm.start_address;
        entry.length           = sm.default_size;
        entry.control_register = sm.control_byte;
        entry.status_register  = 0;
        entry.enable           = sm.enable;
        entry.type             = static_cast<uint8_t>(sm.type);
        sii.syncManagers.push_back(entry);
    }

    // ---- PDO mappings ----
    buildMappings(sii, device.tx_pdos, sii.TxPDO);
    buildMappings(sii, device.rx_pdos, sii.RxPDO);

    // ---- DC category (ETG.2010 Table 12): one 24-byte element per OpMode ----
    if (device.dc)
    {
        for (auto const& op : device.dc->op_modes)
        {
            std::array<uint8_t, 24> el{};  // reserved + absent fields stay zero
            auto put32 = [&](std::size_t off, uint32_t v)
            {
                el[off]     = static_cast<uint8_t>(v & 0xFF);
                el[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
                el[off + 2] = static_cast<uint8_t>((v >> 16) & 0xFF);
                el[off + 3] = static_cast<uint8_t>((v >> 24) & 0xFF);
            };
            auto put16 = [&](std::size_t off, uint16_t v)
            {
                el[off]     = static_cast<uint8_t>(v & 0xFF);
                el[off + 1] = static_cast<uint8_t>((v >> 8) & 0xFF);
            };

            if (op.cycle_time[0]) { put32(0x00, static_cast<uint32_t>(op.cycle_time[0]->value)); }
            if (op.shift_time[0]) { put32(0x04, static_cast<uint32_t>(op.shift_time[0]->value)); }
            if (op.shift_time[1]) { put32(0x08, static_cast<uint32_t>(op.shift_time[1]->value)); }
            if (op.cycle_time[1] and op.cycle_time[1]->factor)
            {
                put16(0x0C, static_cast<uint16_t>(*op.cycle_time[1]->factor));
            }
            put16(0x0E, static_cast<uint16_t>(op.assign_activate));
            if (op.cycle_time[0] and op.cycle_time[0]->factor)
            {
                put16(0x10, static_cast<uint16_t>(*op.cycle_time[0]->factor));
            }
            el[0x12] = sii.registerString(op.name);
            el[0x13] = sii.registerString(op.desc);

            sii.dc.insert(sii.dc.end(), el.begin(), el.end());
        }
    }

    // ---- vendor categories (preserved verbatim) ----
    if (device.eeprom)
    {
        for (auto const& cat : device.eeprom->categories)
        {
            eeprom::RawCategory raw;
            raw.type = static_cast<uint16_t>(cat.cat_no);
            if (not cat.data.empty())
            {
                raw.data = cat.data;
            }
            else if (cat.data_string)
            {
                raw.data.assign(cat.data_string->begin(), cat.data_string->end());
            }
            else if (cat.data_uint)
            {
                uint16_t v = static_cast<uint16_t>(*cat.data_uint);
                raw.data = { static_cast<uint8_t>(v & 0xFF), static_cast<uint8_t>((v >> 8) & 0xFF) };
            }
            else if (cat.data_udint)
            {
                uint32_t v = static_cast<uint32_t>(*cat.data_udint);
                raw.data = { static_cast<uint8_t>(v & 0xFF),         static_cast<uint8_t>((v >> 8) & 0xFF),
                             static_cast<uint8_t>((v >> 16) & 0xFF), static_cast<uint8_t>((v >> 24) & 0xFF) };
            }
            sii.unknownCategories.push_back(std::move(raw));
        }
    }

    return sii;
}

std::vector<uint8_t> buildEepromImage(Device const& device)
{
    if (device.eeprom and not device.eeprom->raw_data.empty())
    {
        return device.eeprom->raw_data;  // ESI already shipped a full image
    }
    return buildSII(device).serialize();
}
}
