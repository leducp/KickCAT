#include <algorithm>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>

#include "kickcat/CoE/protocol.h"
#include "kickcat/debug.h"
#include "kickcat/ESI/Parser.h"
#include "kickcat/utils/xml.h"

using namespace tinyxml2;
using namespace kickcat::xml;

// Signed ESI values are read as BIT_PATTERN: real ETG files write -1 as #xFFFFFFFF.
namespace kickcat::ESI
{

namespace
{
    std::string objectLabel(uint16_t index)
    {
        char buf[24];
        std::snprintf(buf, sizeof(buf), "Object 0x%04x", index);
        return buf;
    }

    // ProductCode, RevisionNo... of the device <Type>; nullopt when absent.
    std::optional<uint32_t> typeAttribute(XMLElement* device, char const* name)
    {
        XMLElement* type = device->FirstChildElement("Type");
        if (type == nullptr)
        {
            return std::nullopt;
        }
        return numberAttribute<uint32_t>(type, name);
    }

    Transitions parseTransitions(XMLElement* parent)
    {
        // ETG.2000 InitCmds only run during these, the ENI-only ones are invalid in an ESI.
        constexpr Transitions ESI_TRANSITIONS = transition::IP | transition::PS | transition::SO
                                              | transition::SP | transition::OP | transition::OS;
        Transitions out = 0;
        for (XMLElement* t = parent->FirstChildElement("Transition"); t != nullptr; t = t->NextSiblingElement("Transition"))
        {
            std::string text = textOf(t);
            transition::Type type = transition::NONE;
            try
            {
                type = transition::fromString(text);
            }
            catch (std::invalid_argument const& e)
            {
                fail(t, e.what());
            }
            if ((type & ESI_TRANSITIONS) == 0)
            {
                fail(t, "Transition '" + text + "' is not allowed in an ESI InitCmd");
            }
            out |= type;
        }
        if (out == 0)
        {
            fail(parent, "missing mandatory <Transition>");
        }
        return out;
    }
}

void Parser::openFile(std::string const& file)
{
    XMLError result = doc_.LoadFile(file.c_str());
    if (result != XML_SUCCESS)
    {
        throw std::runtime_error(doc_.ErrorIDToName(result));
    }
    resolveTopLevel();
}

void Parser::openString(std::string const& xml)
{
    XMLError result = doc_.Parse(xml.c_str());
    if (result != XML_SUCCESS)
    {
        throw std::runtime_error(doc_.ErrorIDToName(result));
    }
    resolveTopLevel();
}

void Parser::resolveTopLevel()
{
    // Reset per-load cached state so values from a previous load don't bleed
    // through if the next ESI omits an optional element.
    vendor_name_.clear();
    profile_no_.clear();
    dtypes_ = nullptr;

    root_ = doc_.RootElement();
    if (root_ == nullptr)
    {
        throw std::invalid_argument("ESI: document has no root element");
    }
    withContext("ESI", [&]()
    {
        vendor_xml_ = require(root_, "Vendor");
        devices_    = require(require(root_, "Descriptions"), "Devices");
    });
    vendor_name_ = optionalText(vendor_xml_, "Name");
}

std::vector<DeviceSummary> Parser::listDevices(std::string const& file)
{
    openFile(file);
    return listDevicesImpl();
}

std::vector<DeviceSummary> Parser::listDevicesString(std::string const& xml)
{
    openString(xml);
    return listDevicesImpl();
}

std::vector<DeviceSummary> Parser::listDevicesImpl()
{
    return withContext("ESI", [&]()
    {
        return all(devices_, "Device", [&](XMLElement* d) { return summarize(d); });
    });
}

DeviceSummary Parser::summarize(XMLElement* device)
{
    DeviceSummary s;
    s.type         = optionalText(device, "Type");
    s.product_code = typeAttribute(device, "ProductCode").value_or(0);
    s.revision_no  = typeAttribute(device, "RevisionNo" ).value_or(0);
    s.serial_no    = typeAttribute(device, "SerialNo"   ).value_or(0);
    s.name         = optionalText(device, "Name");
    return s;
}

XMLElement* Parser::selectDevice(DeviceFilter const& filter)
{
    bool any_filter = filter.type or filter.product_code or filter.revision_no;

    if (not any_filter)
    {
        std::size_t i = 0;
        for (XMLElement* d = devices_->FirstChildElement("Device"); d != nullptr; d = d->NextSiblingElement("Device"))
        {
            if (i == filter.index)
            {
                return d;
            }
            ++i;
        }
        throw std::invalid_argument("device index out of range");
    }

    for (XMLElement* d = devices_->FirstChildElement("Device"); d != nullptr; d = d->NextSiblingElement("Device"))
    {
        if (filter.type and (optionalText(d, "Type") != *filter.type))
        {
            continue;
        }
        if (filter.product_code and (typeAttribute(d, "ProductCode") != filter.product_code))
        {
            continue;
        }
        if (filter.revision_no and (typeAttribute(d, "RevisionNo") != filter.revision_no))
        {
            continue;
        }
        return d;
    }

    throw std::invalid_argument("no device matches filter");
}

Device Parser::loadDevice(std::string const& file, DeviceFilter const& filter)
{
    openFile(file);
    return loadDeviceImpl(filter);
}

Device Parser::loadDeviceString(std::string const& xml, DeviceFilter const& filter)
{
    openString(xml);
    return loadDeviceImpl(filter);
}

Device Parser::loadDeviceImpl(DeviceFilter const& filter)
{
    return withContext("ESI", [&]()
    {
        return buildDeviceFromElement(selectDevice(filter));
    });
}

std::vector<Device> Parser::loadAllDevices(std::string const& file, std::vector<std::string>* errors)
{
    openFile(file);
    std::vector<Device> out;
    std::size_t index = 0;
    for (XMLElement* d = devices_->FirstChildElement("Device"); d != nullptr; d = d->NextSiblingElement("Device"), ++index)
    {
        try
        {
            out.push_back(withContext("ESI", [&]() { return buildDeviceFromElement(d); }));
        }
        catch (std::exception const& e)
        {
            if (errors != nullptr)
            {
                std::string msg = "device[" + std::to_string(index) + "] (";
                msg += optionalText(d, "Type");
                msg += "): ";
                msg += e.what();
                errors->push_back(std::move(msg));
            }
        }
    }
    return out;
}

Device Parser::buildDeviceFromElement(XMLElement* device_node)
{
    profile_no_.clear();  // per-device: don't leak a prior device's value via profile()
    Device device;
    device.vendor_name = vendor_name_;
    // <Vendor>/<Id> is mandatory per ETG.2000 in spirit, but real vendor ESI
    // files (notably some catalog placeholders) ship with <Id></Id> empty.
    // Require the element to be present; allow empty text -> vendor_id = 0
    // to preserve compatibility with those files.
    XMLElement* id = require(vendor_xml_, "Id");
    if (not textOf(id).empty())
    {
        device.vendor_id = numberOf<uint32_t>(id);
    }

    device.type         = optionalText(device_node, "Type");
    device.product_code = typeAttribute(device_node, "ProductCode").value_or(0);
    device.revision_no  = typeAttribute(device_node, "RevisionNo" ).value_or(0);
    device.serial_no    = typeAttribute(device_node, "SerialNo"   ).value_or(0);
    device.name         = optionalText(device_node, "Name");
    device.group_type   = optionalText(device_node, "GroupType");

    // <Profile> is optional in real-world ESIs — Beckhoff IO terminal entries
    // and other simple slaves have no CoE dictionary. Accept devices without
    // <Profile>; the dictionary will only contain the synthesised 0x1C00/PDO
    // mapping objects derived from <Sm>/<Pdo> declarations.
    XMLElement* profile_node = device_node->FirstChildElement("Profile");
    if (profile_node != nullptr)
    {
        profile_no_ = optionalText(profile_node, "ProfileNo");
        if (not profile_no_.empty())
        {
            device.profile_no = requireNumber<uint16_t>(profile_node, "ProfileNo");
        }
    }

    parseSyncManagers(device_node, device.sync_managers);
    parseSyncUnits   (device_node, device.sync_units);
    parseFmmus       (device_node, device.fmmus);
    parseMailbox     (device_node, device.mailbox);
    parsePdos        (device_node, "RxPdo", device.rx_pdos);
    parsePdos        (device_node, "TxPdo", device.tx_pdos);
    parseEeprom      (device_node, device.eeprom);
    parseDc          (device_node, device.dc);
    // Modular-device composition (<Slots>/<ModuleGroups>) is not modeled.

    device.dictionary = buildDictionary(profile_node, device.sync_managers);
    synthesizePdoTargetObjects(device);
    synthesizePdoMappingObjects(device);
    return device;
}

void Parser::parseMailbox(XMLElement* device, std::optional<Mailbox>& out)
{
    // The <Device>/<Mailbox> block is distinct from <Device>/<Info>/<Mailbox>
    // (which carries request/response timeouts). Iterate children only — never
    // pick the one nested under <Info>.
    XMLElement* mbx = device->FirstChildElement("Mailbox");
    if (mbx == nullptr)
    {
        return;
    }

    out.emplace();
    out->data_link_layer = boolAttribute(mbx, "DataLinkLayer");
    out->real_time_mode  = boolAttribute(mbx, "RealTimeMode");

    if (XMLElement* coe = mbx->FirstChildElement("CoE"))
    {
        Mailbox::CoE block;
        block.sdo_info                   = boolAttribute(coe, "SdoInfo");
        block.pdo_assign                 = boolAttribute(coe, "PdoAssign");
        block.pdo_config                 = boolAttribute(coe, "PdoConfig");
        block.pdo_upload                 = boolAttribute(coe, "PdoUpload");
        block.complete_access            = boolAttribute(coe, "CompleteAccess");
        block.segmented_sdo              = boolAttribute(coe, "SegmentedSdo");
        block.diag_history               = boolAttribute(coe, "DiagHistory");
        block.sdo_upload_with_max_length = boolAttribute(coe, "SdoUploadWithMaxLength");
        block.time_distribution          = boolAttribute(coe, "TimeDistribution");
        block.eds_file                   = attributeOf(coe, "EdsFile").value_or("");

        // CoE/Object (obsolete in the XSD) is ignored; modern ESIs use <InitCmd>.
        block.init_cmds = all(coe, "InitCmd", [](XMLElement* ic)
        {
            Mailbox::CoE::InitCmd cmd;
            cmd.transitions = parseTransitions(ic);
            cmd.index    = requireNumber<uint16_t>(ic, "Index");
            cmd.subindex = requireNumber<uint8_t>(ic, "SubIndex");
            XMLElement* data = require(ic, "Data");
            cmd.data = hexBinary(data);
            cmd.adapt_automatically   = boolAttribute(data, "AdaptAutomatically");
            cmd.complete_access       = boolAttribute(ic, "CompleteAccess");
            cmd.overwritten_by_module = boolAttribute(ic, "OverwrittenByModule");
            cmd.comment = optionalText(ic, "Comment");
            return cmd;
        });
        out->coe = std::move(block);
    }

    if (XMLElement* eoe = mbx->FirstChildElement("EoE"))
    {
        Mailbox::EoE block;
        block.ip         = boolAttribute(eoe, "IP");
        block.mac        = boolAttribute(eoe, "MAC");
        block.time_stamp = boolAttribute(eoe, "TimeStamp");
        block.init_cmds = all(eoe, "InitCmd", [](XMLElement* ic)
        {
            Mailbox::EoE::InitCmd cmd;
            cmd.transitions = parseTransitions(ic);
            cmd.type = requireNumber<int32_t>(ic, "Type", BIT_PATTERN);
            cmd.data = hexBinary(require(ic, "Data"));
            cmd.comment = optionalText(ic, "Comment");
            return cmd;
        });
        out->eoe = std::move(block);
    }

    if (mbx->FirstChildElement("FoE") != nullptr)
    {
        out->foe = Mailbox::FoE{};
    }

    if (XMLElement* soe = mbx->FirstChildElement("SoE"))
    {
        Mailbox::SoE block;
        block.channel_count      = numberAttribute<int32_t>(soe, "ChannelCount");
        block.drive_follows_bit3 = boolAttribute(soe, "DriveFollowsBit3Support");
        block.init_cmds = all(soe, "InitCmd", [](XMLElement* ic)
        {
            Mailbox::SoE::InitCmd cmd;
            cmd.transitions = parseTransitions(ic);
            cmd.idn = requireNumber<int32_t>(ic, "IDN", BIT_PATTERN);
            cmd.channel = numberAttribute<int32_t>(ic, "Chn").value_or(0);
            cmd.data = hexBinary(require(ic, "Data"));
            cmd.comment = optionalText(ic, "Comment");
            return cmd;
        });
        out->soe = std::move(block);
    }

    if (XMLElement* aoe = mbx->FirstChildElement("AoE"))
    {
        Mailbox::AoE block;
        block.ads_router            = boolAttribute(aoe, "AdsRouter");
        block.generate_own_net_id   = boolAttribute(aoe, "GenerateOwnNetId");
        block.initialize_own_net_id = boolAttribute(aoe, "InitializeOwnNetId");
        block.init_cmds = all(aoe, "InitCmd", [](XMLElement* ic)
        {
            Mailbox::AoE::InitCmd cmd;
            cmd.transitions = parseTransitions(ic);
            cmd.data = hexBinary(require(ic, "Data"));
            cmd.comment = optionalText(ic, "Comment");
            return cmd;
        });
        out->aoe = std::move(block);
    }

    if (mbx->FirstChildElement("VoE") != nullptr)
    {
        out->voe = Mailbox::VoE{};
    }

    // <Mailbox>/<VendorSpecific> is open vendor content and is not surfaced.
}

namespace
{
    PdoEntry parsePdoEntry(XMLElement* node)
    {
        PdoEntry entry;
        entry.index     = requireNumber<uint16_t>(node, "Index");
        entry.subindex  = optionalNumber<uint8_t>(node, "SubIndex").value_or(0);   // padding entries have none
        entry.bit_len   = requireNumber<uint16_t>(node, "BitLen");
        entry.name      = optionalText(node, "Name");
        entry.comment   = optionalText(node, "Comment");
        entry.data_type = optionalText(node, "DataType");

        entry.fixed                 = boolAttribute(node, "Fixed");
        entry.safety_conn_number    = numberAttribute<int32_t>(node, "SafetyConnNumber");
        entry.safety_pdo_entry_type = attributeOf(node, "SafetyPdoEntryType").value_or("");
        return entry;
    }
}

void Parser::parsePdos(XMLElement* device, char const* element_name, std::vector<Pdo>& out)
{
    for (XMLElement* pdo_node = device->FirstChildElement(element_name); pdo_node != nullptr;
         pdo_node = pdo_node->NextSiblingElement(element_name))
    {
        // Legacy aggregate catalogs declare PDOs by template reference
        // (<RxPdo Ref="...">) with no inline <Index>; skip those rather than
        // rejecting the whole device (the part is defined fully elsewhere).
        if (optionalText(pdo_node, "Index").empty())
        {
            continue;
        }

        Pdo pdo;
        // <Index>/@DependOnSlot/@DependOnSlotGroup are modular-device attributes
        // and are not read.
        pdo.index = requireNumber<uint16_t>(pdo_node, "Index");

        // <Name> is mandatory per PdoType. Multi-language variants (@LcId) are
        // not distinguished; the first <Name> wins.
        pdo.name = requireText(pdo_node, "Name");

        pdo.sm                    = numberAttribute<int32_t>(pdo_node, "Sm");
        pdo.su                    = numberAttribute<int32_t>(pdo_node, "Su");
        pdo.fixed                 = boolAttribute(pdo_node, "Fixed");
        pdo.mandatory             = boolAttribute(pdo_node, "Mandatory");
        pdo.is_virtual            = boolAttribute(pdo_node, "Virtual");
        pdo.os_fac                = numberAttribute<int32_t>(pdo_node, "OSFac");
        pdo.os_min                = numberAttribute<int32_t>(pdo_node, "OSMin");
        pdo.os_max                = numberAttribute<int32_t>(pdo_node, "OSMax");
        pdo.os_index_inc          = numberAttribute<int32_t>(pdo_node, "OSIndexInc");
        pdo.pdo_order             = numberAttribute<int32_t>(pdo_node, "PdoOrder");
        pdo.overwritten_by_module = boolAttribute(pdo_node, "OverwrittenByModule");
        pdo.sra_parameter         = boolAttribute(pdo_node, "SRA_Parameter");
        pdo.safety_conn_number    = numberAttribute<int32_t>(pdo_node, "SafetyConnNumber");
        pdo.safety_pdo_type       = attributeOf(pdo_node, "SafetyPdoType").value_or("");

        pdo.exclude     = all(pdo_node, "Exclude",    [](XMLElement* e) { return numberOf<uint16_t>(e); });
        pdo.excluded_sm = all(pdo_node, "ExcludedSm", [](XMLElement* e) { return numberOf<int32_t>(e, BIT_PATTERN); });
        pdo.entries     = all(pdo_node, "Entry", parsePdoEntry);

        for (auto const& existing : out)
        {
            if (existing.index == pdo.index)
            {
                // Shipped devices re-declare an index on another SM (Beckhoff
                // EL2252 RxPdo 0x1602): keep both, downstream groups per SM.
                esi_warning("ESI: duplicate %s <Index> 0x%04x\n", element_name, pdo.index);
                break;
            }
        }
        out.push_back(std::move(pdo));
    }
}

namespace
{
    bool dictionaryContains(CoE::Dictionary const& dict, uint16_t index)
    {
        for (auto const& obj : dict)
        {
            if (obj.index == index)
            {
                return true;
            }
        }
        return false;
    }
}

// Build the per-PDO mapping object (0x16xx or 0x1Axx) from a parsed Pdo.
// Layout per ETG.1000.6: SubIndex 0 = entry count (uint8), SubIndex N =
// packed uint32 = (Index << 16) | (SubIndex << 8) | BitLen.
CoE::Object Parser::buildMappingObject(Pdo const& pdo, bool is_rx)
{
    CoE::Object obj;
    obj.index = pdo.index;
    obj.code  = CoE::ObjectCode::RECORD;
    if (not pdo.name.empty())
    {
        obj.name = pdo.name;
    }
    else if (is_rx)
    {
        obj.name = "RxPDO Mapping";
    }
    else
    {
        obj.name = "TxPDO Mapping";
    }

    if (pdo.entries.size() > 0xFF)
    {
        throw std::invalid_argument("PDO has more than 255 entries (cannot synthesize mapping)");
    }

    // ETG.1000.6: a PDO mapping object is read-write in PRE_OP (the master may
    // re-map) unless the PDO content is fixed (PdoFixedContent), then read-only.
    uint16_t map_access = CoE::Access::READ | CoE::Access::WRITE_PREOP;
    if (pdo.fixed)
    {
        map_access = CoE::Access::READ;
    }

    CoE::Entry subindex0{0, 8, 0, map_access, CoE::DataType::UNSIGNED8, "Number of entries"};
    subindex0.data = std::malloc(1);
    uint8_t count = static_cast<uint8_t>(pdo.entries.size());
    std::memcpy(subindex0.data, &count, 1);
    obj.entries.push_back(std::move(subindex0));

    uint16_t bitoff = 8;  // SubIndex 0 occupies the first byte (bits 0..7)
    for (std::size_t i = 0; i < pdo.entries.size(); ++i)
    {
        auto const& e = pdo.entries[i];
        if (e.bit_len > 0xFF)
        {
            throw std::invalid_argument("PDO entry BitLen > 255 cannot fit in mapping word");
        }
        CoE::Entry entry;
        entry.subindex    = static_cast<uint8_t>(i + 1);
        entry.bitlen      = 32;
        entry.bitoff      = bitoff;
        entry.access      = map_access;
        entry.type        = CoE::DataType::UNSIGNED32;
        if (not e.name.empty())
        {
            entry.description = e.name;
        }
        else
        {
            entry.description = "Entry " + std::to_string(i + 1);
        }
        entry.data        = std::malloc(sizeof(uint32_t));
        uint32_t packed = CoE::toMappingWord({e.index, e.subindex, static_cast<uint8_t>(e.bit_len)});
        std::memcpy(entry.data, &packed, sizeof(uint32_t));
        obj.entries.push_back(std::move(entry));
        bitoff = static_cast<uint16_t>(bitoff + 32);
    }
    return obj;
}

// Build the SM-assignment object (0x1C12 for RxPDO, 0x1C13 for TxPDO).
// SubIndex 0 = PDO count (uint8), SubIndex N = PDO mapping index (uint16).
CoE::Object Parser::buildAssignmentObject(std::vector<Pdo> const& pdos, uint16_t index, bool is_rx)
{
    CoE::Object obj;
    obj.index = index;
    obj.code  = CoE::ObjectCode::ARRAY;
    if (is_rx)
    {
        obj.name = "RxPDO assign";
    }
    else
    {
        obj.name = "TxPDO assign";
    }

    if (pdos.size() > 0xFF)
    {
        throw std::invalid_argument("more than 255 PDOs assigned to a SyncManager");
    }

    // ETG.1000.6: the SM PDO-assignment object is read-write in PRE_OP so the
    // master can (re)assign PDOs to the SyncManager.
    uint16_t const assign_access = CoE::Access::READ | CoE::Access::WRITE_PREOP;

    CoE::Entry subindex0{0, 8, 0, assign_access, CoE::DataType::UNSIGNED8, "Number of assigned PDOs"};
    subindex0.data = std::malloc(1);
    uint8_t count = static_cast<uint8_t>(pdos.size());
    std::memcpy(subindex0.data, &count, 1);
    obj.entries.push_back(std::move(subindex0));

    uint16_t bitoff = 8;  // SubIndex 0 occupies the first byte (bits 0..7)
    for (std::size_t i = 0; i < pdos.size(); ++i)
    {
        CoE::Entry entry;
        entry.subindex    = static_cast<uint8_t>(i + 1);
        entry.bitlen      = 16;
        entry.bitoff      = bitoff;
        entry.access      = assign_access;
        entry.type        = CoE::DataType::UNSIGNED16;
        entry.description = "PDO " + std::to_string(i + 1);
        entry.data        = std::malloc(sizeof(uint16_t));
        uint16_t pdo_index = pdos[i].index;
        std::memcpy(entry.data, &pdo_index, sizeof(uint16_t));
        obj.entries.push_back(std::move(entry));
        bitoff = static_cast<uint16_t>(bitoff + 16);
    }
    return obj;
}

void Parser::synthesizePdoTargetObjects(Device& device)
{
    // ETG.2010: a <Pdo>/<Entry> fully specifies its mapped object. Legacy terminals
    // (e.g. EL3xxx, 0x3xxx area) describe process data only via <Entry>, never as a
    // <Dictionary> object, so parsePdoMap can't resolve it. Materialize the missing
    // targets from the entry metadata; additive, declared objects are untouched.
    struct Field
    {
        uint16_t      bitlen = 0;
        std::string   name;
        CoE::DataType type = CoE::DataType::UNKNOWN;
        bool          writable = false;  // RxPDO target -> master writes it
    };
    std::map<uint16_t, std::map<uint8_t, Field>> targets;

    auto collect = [&](std::vector<Pdo> const& pdos, bool is_rx)
    {
        for (auto const& pdo : pdos)
        {
            for (auto const& e : pdo.entries)
            {
                if (e.index == 0 or dictionaryContains(device.dictionary, e.index))
                {
                    continue;  // padding gap, or the object is already declared
                }
                Field& field = targets[e.index][e.subindex];
                field.bitlen = e.bit_len;
                field.name   = e.name;
                auto type    = CoE::dataTypeFromLabel(e.data_type);
                if (type.has_value())
                {
                    field.type = *type;
                }
                else if (e.bit_len == 1)
                {
                    field.type = CoE::DataType::BOOLEAN;
                }
                else if (e.bit_len <= 8)
                {
                    field.type = CoE::DataType::UNSIGNED8;
                }
                else if (e.bit_len <= 16)
                {
                    field.type = CoE::DataType::UNSIGNED16;
                }
                else if (e.bit_len <= 32)
                {
                    field.type = CoE::DataType::UNSIGNED32;
                }
                else
                {
                    field.type = CoE::DataType::UNSIGNED64;
                }
                if (is_rx)
                {
                    field.writable = true;
                }
            }
        }
    };
    collect(device.rx_pdos, true);
    collect(device.tx_pdos, false);

    auto accessOf = [](Field const& f)
    {
        if (f.writable)
        {
            return static_cast<uint16_t>(CoE::Access::READ | CoE::Access::WRITE | CoE::Access::RxPDO);
        }
        return static_cast<uint16_t>(CoE::Access::READ | CoE::Access::TxPDO);
    };

    for (auto const& [index, fields] : targets)
    {
        uint8_t max_sub = 0;
        for (auto const& [sub, field] : fields)
        {
            if (sub > max_sub)
            {
                max_sub = sub;
            }
        }

        CoE::Object obj;
        obj.index = index;

        if (max_sub == 0)
        {
            // Single value mapped at subindex 0 -> VAR. Storage is left to
            // CoE::materializeStorage (host/sim startup), as for any data object.
            Field const& field = fields.at(0);
            obj.code = CoE::ObjectCode::VAR;
            obj.name = field.name;
            obj.entries.emplace_back(0, field.bitlen, 0, accessOf(field), field.type, field.name);
            device.dictionary.push_back(std::move(obj));
            continue;
        }

        obj.code = CoE::ObjectCode::RECORD;
        obj.name = "PDO data";
        CoE::Entry count{0, 8, 0, CoE::Access::READ, CoE::DataType::UNSIGNED8, "Number of entries"};
        count.data = std::malloc(1);
        std::memcpy(count.data, &max_sub, 1);
        obj.entries.push_back(std::move(count));

        uint16_t bitoff = 8;
        for (auto const& [sub, field] : fields)
        {
            if (sub == 0)
            {
                continue;  // a record's subindex 0 is the count, not a data field
            }
            obj.entries.emplace_back(sub, field.bitlen, bitoff, accessOf(field), field.type, field.name);
            bitoff = static_cast<uint16_t>(bitoff + field.bitlen);
        }
        device.dictionary.push_back(std::move(obj));
    }
}

void Parser::synthesizePdoMappingObjects(Device& device)
{
    // ETG.2010 Table 15: PDO mapping (0x16xx/0x1Axx) is defined by <Pdo>/<Entry>,
    // authoritative over an explicit <Object> of the same index. Override only on a
    // genuine conflict, so consistent slaves keep the explicit object's metadata; an
    // unrepresentable PDO is skipped rather than failing the slave.
    auto mappingsAgree = [](CoE::Object const& lhs, CoE::Object const& rhs)
    {
        if (lhs.entries.size() != rhs.entries.size())
        {
            return false;
        }
        for (std::size_t i = 1; i < rhs.entries.size(); ++i)
        {
            if ((lhs.entries[i].bitlen != 32) or (rhs.entries[i].bitlen != 32)
                or (lhs.entries[i].data == nullptr) or (rhs.entries[i].data == nullptr))
            {
                return false;
            }
            uint32_t a;
            uint32_t b;
            std::memcpy(&a, lhs.entries[i].data, sizeof(uint32_t));
            std::memcpy(&b, rhs.entries[i].data, sizeof(uint32_t));
            if (a != b)
            {
                return false;
            }
        }
        return true;
    };

    // A declared object's slot count is the master's re-map/re-assign capacity (e.g.
    // DT1600's 16 slots vs a 4-entry default PDO). Grow the synthesized object to that
    // capacity so a master can re-map beyond the default; spare slots are empty but
    // present and keep the slot template (width/type/access) of the last active entry.
    auto padToCapacity = [](CoE::Object& obj, std::size_t capacity)
    {
        if (obj.entries.empty())
        {
            return;
        }
        // Take the slot template from the last active entry before growing the vector.
        uint16_t const bitlen = obj.entries.back().bitlen;
        uint16_t const access = obj.entries.back().access;
        CoE::DataType const type = obj.entries.back().type;
        uint16_t bitoff = static_cast<uint16_t>(obj.entries.back().bitoff + bitlen);
        for (std::size_t sub = obj.entries.size(); sub < capacity; ++sub)
        {
            CoE::Entry e{static_cast<uint8_t>(sub), bitlen, bitoff, access, type,
                         "Entry " + std::to_string(sub)};
            e.data = std::calloc(1, (bitlen + 7u) / 8u);  // empty slot
            obj.entries.push_back(std::move(e));
            bitoff = static_cast<uint16_t>(bitoff + bitlen);
        }
    };

    auto synthesize = [&](std::vector<Pdo> const& pdos, bool is_rx)
    {
        for (auto const& pdo : pdos)
        {
            try
            {
                auto obj = buildMappingObject(pdo, is_rx);
                CoE::Object* existing = nullptr;
                for (auto& o : device.dictionary)
                {
                    if (o.index == pdo.index) { existing = &o; break; }
                }
                if (existing == nullptr)
                {
                    device.dictionary.push_back(std::move(obj));
                }
                else if (not pdo.entries.empty() and not mappingsAgree(*existing, obj))
                {
                    padToCapacity(obj, existing->entries.size());
                    *existing = std::move(obj);
                }
            }
            catch (std::exception const& e)
            {
                esi_warning("Skipping PDO mapping 0x%04x: %s\n", pdo.index, e.what());
            }
        }
    };
    synthesize(device.rx_pdos, true);
    synthesize(device.tx_pdos, false);

    // ETG.2010 Table 14: only PDOs "mapped by default" (carrying @Sm) belong to a
    // SyncManager's assignment, at 0x1C10 + SM index. @Sm is authoritative; override
    // an explicit assignment object only when it disagrees (e.g. an over-assigning
    // shared dictionary).
    auto assignmentsAgree = [](CoE::Object const& lhs, CoE::Object const& rhs)
    {
        if (lhs.entries.size() != rhs.entries.size())
        {
            return false;
        }
        for (std::size_t i = 1; i < rhs.entries.size(); ++i)
        {
            if ((lhs.entries[i].data == nullptr) or (rhs.entries[i].data == nullptr))
            {
                return false;
            }
            uint16_t a;
            uint16_t b;
            std::memcpy(&a, lhs.entries[i].data, sizeof(uint16_t));
            std::memcpy(&b, rhs.entries[i].data, sizeof(uint16_t));
            if (a != b)
            {
                return false;
            }
        }
        return true;
    };

    auto synthesizeAssignment = [&](std::vector<Pdo> const& pdos, bool is_rx)
    {
        std::vector<int32_t> sms;
        for (auto const& pdo : pdos)
        {
            if (pdo.sm.has_value() and std::find(sms.begin(), sms.end(), *pdo.sm) == sms.end())
            {
                sms.push_back(*pdo.sm);
            }
        }
        for (int32_t sm : sms)
        {
            std::vector<Pdo> group;
            for (auto const& pdo : pdos)
            {
                if (pdo.sm.has_value() and *pdo.sm == sm)
                {
                    group.push_back(pdo);
                }
            }
            uint16_t obj_index = static_cast<uint16_t>(0x1C10 + sm);
            auto obj = buildAssignmentObject(group, obj_index, is_rx);
            CoE::Object* existing = nullptr;
            for (auto& o : device.dictionary)
            {
                if (o.index == obj_index) { existing = &o; break; }
            }
            if (existing == nullptr)
            {
                device.dictionary.push_back(std::move(obj));
            }
            else if (not assignmentsAgree(*existing, obj))
            {
                padToCapacity(obj, existing->entries.size());  // preserve declared re-assign capacity
                *existing = std::move(obj);
            }
        }
    };
    synthesizeAssignment(device.rx_pdos, true);
    synthesizeAssignment(device.tx_pdos, false);

    // ETG.1000.6 Tables 74/75: a mapping entry references object:subindex. Vendor
    // ESI sometimes names a subindex absent from the target object's DataType while
    // the value lives at another (e.g. EL4004 maps 0x7000:17, but later revisions
    // declare the value at 0x7000:1). The object dictionary is the structural
    // authority, so retarget a dangling reference to the object's unique entry of
    // matching bit length.
    for (auto& obj : device.dictionary)
    {
        bool const is_mapping = (obj.index >= 0x1600 and obj.index <= 0x17FF)
                             or (obj.index >= 0x1A00 and obj.index <= 0x1BFF);
        if (not is_mapping)
        {
            continue;
        }
        for (auto& entry : obj.entries)
        {
            if (entry.subindex == 0 or entry.data == nullptr or entry.bitlen != 32)
            {
                continue;
            }
            uint32_t mapping;
            std::memcpy(&mapping, entry.data, sizeof(uint32_t));
            CoE::PdoMappingEntry me = CoE::fromMappingWord(mapping);
            uint16_t index = me.index;
            uint8_t  sub   = me.subindex;
            uint8_t  bits  = me.bitlen;
            if (index == 0)
            {
                continue;  // padding gap
            }

            auto [target_obj, target_entry] = CoE::findObject(device.dictionary, index, sub);
            if (target_entry != nullptr or target_obj == nullptr)
            {
                continue;  // already resolves, or the whole object is absent
            }

            int found_sub = -1;
            int matches    = 0;
            for (auto const& te : target_obj->entries)
            {
                bool const is_data_sub = (te.subindex >= 1) or (target_obj->code == CoE::ObjectCode::VAR);
                if (is_data_sub and te.bitlen == bits)
                {
                    matches++;
                    found_sub = te.subindex;
                }
            }
            if (matches != 1)
            {
                continue;
            }

            uint32_t fixed = CoE::toMappingWord({index, static_cast<uint8_t>(found_sub), bits});
            std::memcpy(entry.data, &fixed, sizeof(uint32_t));
            esi_warning("PDO 0x%04x: retargeted mapping 0x%04x:%u -> 0x%04x:%d (object declares data there)\n",
                obj.index, index, sub, index, found_sub);
        }
    }
}

void Parser::parseEeprom(XMLElement* device, std::optional<Eeprom>& out)
{
    XMLElement* eep = device->FirstChildElement("Eeprom");
    if (eep == nullptr)
    {
        return;
    }

    Eeprom block;
    block.assign_to_pdi = boolAttribute(eep, "AssignToPdi");

    // EepromType is an xs:choice between raw <Data> and the structured form
    // (<ByteSize>+<ConfigData>+…). Reject documents carrying both so the error
    // names the real problem.
    XMLElement* data_raw  = eep->FirstChildElement("Data");
    XMLElement* byte_size = eep->FirstChildElement("ByteSize");
    if (data_raw != nullptr and byte_size != nullptr)
    {
        fail(eep, "<Data> (raw form) and <ByteSize> (structured form) are mutually exclusive");
    }
    if (data_raw != nullptr)
    {
        block.raw_data = hexBinary(data_raw);
        out = std::move(block);
        return;
    }

    // ByteSize and ConfigData are mandatory children of the structured form.
    block.byte_size   = requireNumber<int32_t>(eep, "ByteSize", BIT_PATTERN);
    block.config_data = hexBinary(require(eep, "ConfigData"));

    if (XMLElement* cfg2 = eep->FirstChildElement("ConfigData2"))
    {
        block.config_data2 = hexBinary(cfg2);
    }
    if (XMLElement* boot = eep->FirstChildElement("BootStrap"))
    {
        block.bootstrap = hexBinary(boot);
    }
    block.categories = all(eep, "Category", [](XMLElement* cat)
    {
        Eeprom::Category c;
        c.cat_no = requireNumber<int32_t>(cat, "CatNo", BIT_PATTERN);
        c.preserve_online_data = boolAttribute(cat, "PreserveOnlineData");
        // Payload is an xs:choice; pick by element presence, not text content,
        // so an empty <DataString/> reads as an empty string rather than absent.
        // The numeric forms still require a value.
        if (XMLElement* d = cat->FirstChildElement("Data"))
        {
            c.data = hexBinary(d);
        }
        else if (XMLElement* s = cat->FirstChildElement("DataString"))
        {
            c.data_string = textOf(s);
        }
        else if (cat->FirstChildElement("DataUINT") != nullptr)
        {
            c.data_uint = requireNumber<int32_t>(cat, "DataUINT", BIT_PATTERN);
        }
        else if (cat->FirstChildElement("DataUDINT") != nullptr)
        {
            c.data_udint = requireNumber<int32_t>(cat, "DataUDINT", BIT_PATTERN);
        }
        else
        {
            fail(cat, "missing payload (expected one of <Data>, <DataString>, <DataUINT>, <DataUDINT>)");
        }
        return c;
    });

    out = std::move(block);
}

namespace
{
    // An empty <CycleTimeSyncN/> or <ShiftTimeSyncN/> keeps its value at 0.
    int32_t syncTimeValue(XMLElement* node)
    {
        if (textOf(node).empty())
        {
            return 0;
        }
        return numberOf<int32_t>(node, BIT_PATTERN);
    }

    std::optional<OpMode::SyncTime> parseSyncTime(XMLElement* parent, char const* name)
    {
        XMLElement* node = parent->FirstChildElement(name);
        if (node == nullptr)
        {
            return std::nullopt;
        }
        OpMode::SyncTime st;
        st.value  = syncTimeValue(node);
        st.factor = numberAttribute<int32_t>(node, "Factor");
        return st;
    }

    std::optional<OpMode::ShiftTime> parseShiftTime(XMLElement* parent, char const* name)
    {
        XMLElement* node = parent->FirstChildElement(name);
        if (node == nullptr)
        {
            return std::nullopt;
        }
        OpMode::ShiftTime st;
        st.value  = syncTimeValue(node);
        st.factor = numberAttribute<int32_t>(node, "Factor");
        if (attributeOf(node, "Input"))
        {
            st.input = boolAttribute(node, "Input");
        }
        st.output_delay_time = numberAttribute<int32_t>(node, "OutputDelayTime");
        st.input_delay_time  = numberAttribute<int32_t>(node, "InputDelayTime");
        return st;
    }
}

void Parser::parseDc(XMLElement* device, std::optional<Dc>& out)
{
    XMLElement* dc = device->FirstChildElement("Dc");
    if (dc == nullptr)
    {
        return;
    }

    Dc block;
    block.unknown_frmw              = boolAttribute(dc, "UnknownFRMW");
    block.unknown_64bit             = boolAttribute(dc, "Unknown64Bit");
    block.external_ref_clock        = boolAttribute(dc, "ExternalRefClock");
    block.potential_reference_clock = boolAttribute(dc, "PotentialReferenceClock");
    block.time_loop_control_only    = boolAttribute(dc, "TimeLoopControlOnly");
    block.pdo_oversampling          = boolAttribute(dc, "PdoOversampling");

    block.op_modes = all(dc, "OpMode", [](XMLElement* om)
    {
        static char const* const CYCLE_NAMES[4] = {"CycleTimeSync0", "CycleTimeSync1", "CycleTimeSync2", "CycleTimeSync3"};
        static char const* const SHIFT_NAMES[4] = {"ShiftTimeSync0", "ShiftTimeSync1", "ShiftTimeSync2", "ShiftTimeSync3"};

        OpMode mode;
        mode.name                = requireText(om, "Name");
        mode.desc                = optionalText(om, "Desc");
        mode.assign_activate     = requireNumber<uint32_t>(om, "AssignActivate");
        mode.activate_additional = optionalNumber<uint32_t>(om, "ActivateAdditional");
        for (int i = 0; i < 4; ++i)
        {
            mode.cycle_time[i] = parseSyncTime (om, CYCLE_NAMES[i]);
            mode.shift_time[i] = parseShiftTime(om, SHIFT_NAMES[i]);
        }

        // <OpMode>/<Sm No="..">: @No plus the <Pdo>/@OSFac oversampling map. The
        // SyncType/CycleTime/ShiftTime children are obsolete in the XSD; skipped.
        mode.sm_configs = all(om, "Sm", [](XMLElement* sm)
        {
            OpMode::SmConfig cfg;
            std::optional<int32_t> no = numberAttribute<int32_t>(sm, "No");
            if (not no)
            {
                fail(sm, "missing mandatory @No");
            }
            cfg.no = *no;
            cfg.pdos = all(sm, "Pdo", [](XMLElement* p)
            {
                OpMode::SmConfig::PdoRef ref;
                ref.index  = numberOf<uint16_t>(p);
                ref.os_fac = numberAttribute<int32_t>(p, "OSFac");
                return ref;
            });
            return cfg;
        });
        return mode;
    });

    out = std::move(block);
}

void Parser::parseSyncManagers(XMLElement* device, std::vector<SmInfo>& out)
{
    out = all(device, "Sm", [](XMLElement* sm)
    {
        SmInfo entry;
        std::string text = textOf(sm);
        if (not text.empty())
        {
            try
            {
                fromString(text, entry.type);
            }
            catch (std::invalid_argument const& e)
            {
                fail(sm, e.what());
            }
        }

        entry.min_size      = numberAttribute<uint16_t>(sm, "MinSize"     ).value_or(0);
        entry.max_size      = numberAttribute<uint16_t>(sm, "MaxSize"     ).value_or(0);
        entry.default_size  = numberAttribute<uint16_t>(sm, "DefaultSize" ).value_or(0);
        entry.start_address = numberAttribute<uint16_t>(sm, "StartAddress").value_or(0);
        entry.control_byte  = numberAttribute<uint8_t> (sm, "ControlByte" ).value_or(0);
        entry.enable        = numberAttribute<uint8_t> (sm, "Enable"      ).value_or(0);
        entry.is_virtual    = boolAttribute(sm, "Virtual");
        entry.op_only       = boolAttribute(sm, "OpOnly");
        return entry;
    });
}

void Parser::parseSyncUnits(XMLElement* device, std::vector<SyncUnit>& out)
{
    out = all(device, "Su", [](XMLElement* su)
    {
        SyncUnit entry;
        entry.separate_su          = boolAttribute(su, "SeparateSu");
        entry.separate_frame       = boolAttribute(su, "SeparateFrame");
        entry.frame_repeat_support = boolAttribute(su, "FrameRepeatSupport");
        return entry;
    });
}

void Parser::parseFmmus(XMLElement* device, std::vector<Fmmu>& out)
{
    out = all(device, "Fmmu", [](XMLElement* fmmu)
    {
        Fmmu entry;
        std::string text = textOf(fmmu);
        if (not text.empty())
        {
            try
            {
                fromString(text, entry.type);
            }
            catch (std::invalid_argument const& e)
            {
                fail(fmmu, e.what());
            }
        }

        if (std::optional<uint8_t> sm = numberAttribute<uint8_t>(fmmu, "Sm"))
        {
            entry.sm = *sm;
        }
        if (std::optional<uint8_t> su = numberAttribute<uint8_t>(fmmu, "Su"))
        {
            entry.su = *su;
        }
        entry.op_only = boolAttribute(fmmu, "OpOnly");
        return entry;
    });
}

CoE::Dictionary Parser::buildDictionary(XMLElement* profile, std::vector<SmInfo> const& sms)
{
    CoE::Dictionary out;

    // Profile is optional (see loadDeviceImpl). If absent, skip the inline
    // dictionary entirely and proceed straight to the synthesised 0x1C00 below.
    // DataTypes is also optional per the XSD (DictionaryType minOccurs=0); a
    // device whose Objects only reference basic types has no <DataTypes>.
    // Objects is required when Dictionary is present.
    dtypes_ = nullptr;
    XMLElement* dictionary = nullptr;
    if (profile != nullptr)
    {
        dictionary = profile->FirstChildElement("Dictionary");
    }
    if (dictionary != nullptr)
    {
        dtypes_ = dictionary->FirstChildElement("DataTypes");
        // <Dictionary> itself is optional, but when present the XSD requires an
        // <Objects> child (DictionaryType sequence).
        XMLElement* objects = require(dictionary, "Objects");
        for (XMLElement* node_object = objects->FirstChildElement(); node_object != nullptr; node_object = node_object->NextSiblingElement())
        {
            uint16_t index = requireNumber<uint16_t>(node_object, "Index");
            out.push_back(withContext(objectLabel(index), [&]() { return createObject(node_object, index); }));
        }
    }

    // Synthesize CoE object 0x1C00 (Sync Manager Communication Type) from the
    // device's <Sm> declarations so callers of loadFile/loadString still get an
    // SM-type array in their CoE::Dictionary. An explicit 0x1C00 in the ESI wins.
    if (dictionaryContains(out, 0x1C00))
    {
        return out;
    }
    if (sms.size() > 0xFF)
    {
        throw std::invalid_argument("more than 255 <Sm> entries cannot fit in 0x1C00 SubIndex space");
    }

    CoE::Object sms_type;
    sms_type.index = 0x1c00;
    sms_type.code  = CoE::ObjectCode::ARRAY;
    sms_type.name  = "Sync manager type";
    sms_type.entries.push_back(CoE::Entry{0, 8, 0, CoE::Access::READ, CoE::DataType::UNSIGNED8, "Subindex 0"});

    for (std::size_t i = 0; i < sms.size(); ++i)
    {
        CoE::Entry entry;
        entry.subindex    = static_cast<uint8_t>(i + 1);
        entry.access      = CoE::Access::READ;
        entry.bitlen      = 8;
        entry.bitoff      = static_cast<uint16_t>((i + 1) * 8);
        entry.description = "Subindex " + std::to_string(i + 1);
        entry.type        = CoE::DataType::UNSIGNED8;
        entry.data        = std::malloc(1);

        uint8_t type = static_cast<uint8_t>(sms[i].type);
        std::memcpy(entry.data, &type, 1);

        sms_type.entries.push_back(std::move(entry));
    }

    auto& subindex0 = sms_type.entries.at(0);
    subindex0.data = std::malloc(1);
    uint8_t array_size = static_cast<uint8_t>(sms.size());
    std::memcpy(subindex0.data, &array_size, 1);
    out.push_back(std::move(sms_type));

    return out;
}

CoE::Dictionary Parser::loadFile(std::string const& file)
{
    return loadDevice(file, {}).dictionary;
}

CoE::Dictionary Parser::loadString(std::string const& xml)
{
    return loadDeviceString(xml, {}).dictionary;
}

void Parser::loadDefaultData(XMLElement* node, CoE::Object& obj, CoE::Entry& entry)
{
    XMLElement* node_info = node->FirstChildElement("Info");
    if (node_info == nullptr)
    {
        return;
    }

    XMLElement* node_default_data = node_info->FirstChildElement("DefaultData");
    if (node_default_data != nullptr)
    {
        // VISIBLE_STRING and other binary defaults share the same loader; the
        // ESI <DefaultData> hex-binary content is already in the natural byte
        // order (ASCII bytes for strings, little-endian for numerics).
        std::vector<uint8_t> data = hexBinary(node_default_data);

        if (data.size() != ((entry.bitlen + 7u) / 8u))
        {
            esi_warning("Cannot load default data for 0x%04x.%d, expected size mismatch.\n"
                    "-> Got %ld bits, expected: %d bit\n"
                    "==> Skipping entry\n",
                obj.index, entry.subindex,
                data.size() * 8, entry.bitlen);
            return;
        }
        if (data.empty())
        {
            return;
        }
        entry.data = std::malloc(data.size());
        std::memcpy(entry.data, data.data(), data.size());
        return;
    }

    std::string default_value = optionalText(node_info, "DefaultValue");
    if (not default_value.empty())
    {
        uint32_t size = (entry.bitlen + 7u) / 8u;  // sub-byte entries occupy 1 byte
        if (size == 0)
        {
            return;
        }
        int64_t value = parseInteger(node_info->FirstChildElement("DefaultValue"), default_value);
        uint32_t copy_size = std::min<uint32_t>(size, sizeof(int64_t));
        entry.data = std::malloc(size);
        if (copy_size < size)
        {
            std::memset(entry.data, 0, size);
        }
        std::memcpy(entry.data, &value, copy_size);
    }
}

uint16_t Parser::loadAccess(XMLElement* node)
{
    uint16_t flags = 0;

    XMLElement* node_flags = node->FirstChildElement("Flags");
    if (node_flags == nullptr)
    {
        // A missing <Flags> means default access, like an empty <Flags/> (access=0
        // would make the object unreadable in the emulated-slave OD).
        return CoE::Access::READ;
    }

    XMLElement* node_access = node_flags->FirstChildElement("Access");
    if (node_access != nullptr)
    {
        std::string access = textOf(node_access);
        if (access == "rw" or access == "ro")
        {
            flags |= CoE::Access::READ;
        }
        if (access == "rw" or access == "wo")
        {
            flags |= CoE::Access::WRITE;
        }

        auto parseRestrictions = [](std::optional<std::string> raw_restrictions) -> uint16_t
        {
            if (not raw_restrictions)
            {
                return CoE::Access::READ;
            }

            std::string restrictions = *raw_restrictions;
            std::transform(restrictions.begin(), restrictions.end(), restrictions.begin(),
                [](char c){ return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });

            uint16_t result = 0;
            if (restrictions.find("preop")  != std::string::npos) { result |= CoE::Access::READ_PREOP;  }
            if (restrictions.find("safeop") != std::string::npos) { result |= CoE::Access::READ_SAFEOP; }
            if (restrictions.find("_op")    != std::string::npos) { result |= CoE::Access::READ_OP;     }
            if (restrictions.find("op") == 0)                     { result |= CoE::Access::READ_OP;     }

            return result;
        };

        uint16_t restrictions_mask = 0;
        restrictions_mask |= (parseRestrictions(attributeOf(node_access, "ReadRestrictions"))  << 0);
        restrictions_mask |= (parseRestrictions(attributeOf(node_access, "WriteRestrictions")) << 3);

        flags &= restrictions_mask;
    }
    else
    {
        flags |= CoE::Access::READ;
    }

    for (char c : optionalText(node_flags, "PdoMapping"))
    {
        if (std::tolower(static_cast<unsigned char>(c)) == 'r')
        {
            flags |= CoE::Access::RxPDO;
        }
        if (std::tolower(static_cast<unsigned char>(c)) == 't')
        {
            flags |= CoE::Access::TxPDO;
        }
    }

    if (optionalText(node_flags, "Backup") == "1")
    {
        flags |= CoE::Access::BACKUP;
    }
    if (optionalText(node_flags, "Setting") == "1")
    {
        flags |= CoE::Access::SETTING;
    }

    return flags;
}

namespace
{
    CoE::DataType enumTypeFromBitSize(uint16_t bitsize)
    {
        switch (bitsize)
        {
            case 1:  { return CoE::DataType::BOOLEAN;    }
            case 2:  { return CoE::DataType::BIT2;       }
            case 3:  { return CoE::DataType::BIT3;       }
            case 4:  { return CoE::DataType::BIT4;       }
            case 5:  { return CoE::DataType::BIT5;       }
            case 6:  { return CoE::DataType::BIT6;       }
            case 7:  { return CoE::DataType::BIT7;       }
            case 8:  { return CoE::DataType::UNSIGNED8;  }
            case 16: { return CoE::DataType::UNSIGNED16; }
            case 24: { return CoE::DataType::UNSIGNED24; }
            case 32: { return CoE::DataType::UNSIGNED32; }
            case 40: { return CoE::DataType::UNSIGNED40; }
            case 48: { return CoE::DataType::UNSIGNED48; }
            case 56: { return CoE::DataType::UNSIGNED56; }
            case 64: { return CoE::DataType::UNSIGNED64; }
            default: { return CoE::DataType::UNKNOWN;    }
        }
    }

    // ETG.1000.6 standard array of element, held as a single value (OCTET STRING is ARRAY OF BYTE).
    CoE::DataType standardArrayType(CoE::DataType element)
    {
        switch (element)
        {
            case CoE::DataType::BYTE:
            case CoE::DataType::UNSIGNED8:  { return CoE::DataType::OCTET_STRING;   }
            case CoE::DataType::UNSIGNED16: { return CoE::DataType::UNICODE_STRING; }
            case CoE::DataType::INTEGER8:   { return CoE::DataType::ARRAY_OF_SINT;  }
            case CoE::DataType::INTEGER16:  { return CoE::DataType::ARRAY_OF_INT;   }
            case CoE::DataType::INTEGER32:  { return CoE::DataType::ARRAY_OF_DINT;  }
            case CoE::DataType::UNSIGNED32: { return CoE::DataType::ARRAY_OF_UDINT; }
            default:                        { return CoE::DataType::UNKNOWN;        }
        }
    }

    // 0 when the type has no fixed width (strings, complex, unknown).
    uint16_t basicBitSize(CoE::DataType type)
    {
        switch (type)
        {
            case CoE::DataType::BOOLEAN:    { return 1;  }
            case CoE::DataType::BIT2:       { return 2;  }
            case CoE::DataType::BIT3:       { return 3;  }
            case CoE::DataType::BIT4:       { return 4;  }
            case CoE::DataType::BIT5:       { return 5;  }
            case CoE::DataType::BIT6:       { return 6;  }
            case CoE::DataType::BIT7:       { return 7;  }
            case CoE::DataType::BIT8:       { return 8;  }
            case CoE::DataType::BYTE:       { return 8;  }
            case CoE::DataType::WORD:       { return 16; }
            case CoE::DataType::DWORD:      { return 32; }
            case CoE::DataType::INTEGER8:   { return 8;  }
            case CoE::DataType::INTEGER16:  { return 16; }
            case CoE::DataType::INTEGER24:  { return 24; }
            case CoE::DataType::INTEGER32:  { return 32; }
            case CoE::DataType::INTEGER40:  { return 40; }
            case CoE::DataType::INTEGER48:  { return 48; }
            case CoE::DataType::INTEGER56:  { return 56; }
            case CoE::DataType::INTEGER64:  { return 64; }
            case CoE::DataType::UNSIGNED8:  { return 8;  }
            case CoE::DataType::UNSIGNED16: { return 16; }
            case CoE::DataType::UNSIGNED24: { return 24; }
            case CoE::DataType::UNSIGNED32: { return 32; }
            case CoE::DataType::UNSIGNED40: { return 40; }
            case CoE::DataType::UNSIGNED48: { return 48; }
            case CoE::DataType::UNSIGNED56: { return 56; }
            case CoE::DataType::UNSIGNED64: { return 64; }
            case CoE::DataType::REAL32:     { return 32; }
            case CoE::DataType::REAL64:     { return 64; }
            default:                        { return 0;  }
        }
    }
}

CoE::DataType Parser::resolveType(std::string const& type_name, int depth)
{
    if (depth > MAX_TYPE_DEPTH)
    {
        throw std::invalid_argument("<BaseType> recursion exceeds depth " + std::to_string(MAX_TYPE_DEPTH)
            + " resolving '" + type_name + "' (cycle?)");
    }

    if (auto basic = CoE::dataTypeFromLabel(type_name))
    {
        return *basic;
    }

    if (type_name.find("STRING") != std::string::npos)
    {
        return CoE::DataType::VISIBLE_STRING;
    }

    if (dtypes_ == nullptr)
    {
        return CoE::DataType::UNKNOWN;
    }
    for (XMLElement* dtype = dtypes_->FirstChildElement(); dtype != nullptr; dtype = dtype->NextSiblingElement())
    {
        if (optionalText(dtype, "Name") != type_name)
        {
            continue;
        }
        if (dtype->FirstChildElement("SubItem") or dtype->FirstChildElement("ArrayInfo"))
        {
            return CoE::DataType::UNKNOWN;
        }
        if (dtype->FirstChildElement("BaseType") != nullptr)
        {
            return resolveType(requireText(dtype, "BaseType"), depth + 1);
        }

        // Enum DataType without <BaseType> (EtherCATBase.xsd: BaseType is
        // minOccurs=0): the value width is its mandatory <BitSize>.
        if (dtype->FirstChildElement("EnumInfo"))
        {
            return enumTypeFromBitSize(requireNumber<uint16_t>(dtype, "BitSize"));
        }
        break;
    }

    return CoE::DataType::UNKNOWN;
}

std::tuple<CoE::DataType, uint16_t, uint16_t> Parser::parseType(XMLElement* node)
{
    std::string type_text = optionalText(node, "Type");
    if (type_text.empty())
    {
        type_text = optionalText(node, "BaseType");
    }
    if (type_text.empty())
    {
        return {CoE::DataType::UNKNOWN, 0, 0};
    }

    CoE::DataType type = resolveType(type_text);
    if (type == CoE::DataType::UNKNOWN)
    {
        return {CoE::DataType::UNKNOWN, 0, 0};
    }

    uint16_t bitlen = requireNumber<uint16_t>(node, "BitSize");
    uint16_t bitoff = optionalNumber<uint16_t>(node, "BitOffs").value_or(0);
    return {type, bitlen, bitoff};
}

XMLElement* Parser::findNodeType(XMLElement* node)
{
    std::string type = requireText(node, "Type");
    if (dtypes_ == nullptr)
    {
        return nullptr;
    }
    for (XMLElement* dtype = dtypes_->FirstChildElement(); dtype != nullptr; dtype = dtype->NextSiblingElement())
    {
        if (optionalText(dtype, "Name") == type)
        {
            return dtype;
        }
    }
    return nullptr;
}

CoE::Object Parser::createObject(XMLElement* node, uint16_t index)
{
    CoE::Object object;
    object.index = index;
    object.name  = requireText(node, "Name");

    auto setValue = [&](CoE::DataType type, uint16_t bitlen, uint16_t bitoff)
    {
        object.code = CoE::ObjectCode::VAR;
        object.entries.resize(1);
        CoE::Entry& entry = object.entries.at(0);
        entry.subindex = 0;
        entry.bitlen   = bitlen;
        entry.bitoff   = bitoff;
        entry.type     = type;
        entry.access   = loadAccess(node);
        loadDefaultData(node, object, entry);
    };

    auto [type, bitlen, bitoff] = parseType(node);
    if (CoE::isBasic(type))
    {
        setValue(type, bitlen, bitoff);
        return object;
    }

    XMLElement* node_type = findNodeType(node);
    if (node_type == nullptr)
    {
        if (dtypes_ == nullptr)
        {
            fail(node, "references a user-defined <Type> but no <DataTypes> section is present in <Dictionary>");
        }
        fail(node, "unresolved <Type> reference");
    }
    if ((node_type->FirstChildElement("SubItem") == nullptr) and (node_type->FirstChildElement("ArrayInfo") != nullptr))
    {
        setValue(standardArrayType(resolveType(requireText(node_type, "BaseType"))), requireNumber<uint16_t>(node, "BitSize"), 0);
        return object;
    }
    for (XMLElement* node_subitem = node_type->FirstChildElement("SubItem"); node_subitem != nullptr;
         node_subitem = node_subitem->NextSiblingElement("SubItem"))
    {
        CoE::Entry entry;
        entry.description = optionalText(node_subitem, "Name");

        auto [subitem_type, subitem_bitlen, subitem_bitoff] = parseType(node_subitem);
        if (CoE::isBasic(subitem_type))
        {
            object.code = CoE::ObjectCode::RECORD;

            entry.type     = subitem_type;
            entry.bitlen   = subitem_bitlen;
            entry.bitoff   = subitem_bitoff;
            entry.subindex = requireNumber<uint8_t>(node_subitem, "SubIdx");
            entry.access   = loadAccess(node_subitem);

            object.entries.push_back(std::move(entry));
        }
        else
        {
            object.code = CoE::ObjectCode::ARRAY;

            XMLElement* node_array_type = findNodeType(node_subitem);
            if (node_array_type == nullptr)
            {
                fail(node_subitem, "unresolved <Type> reference");
            }
            auto [array_type, array_bitlen, array_bitoff] = parseType(node_array_type);
            entry.type   = array_type;
            entry.bitlen = array_bitlen;
            entry.bitoff = array_bitoff;
            entry.access = loadAccess(node_subitem);

            XMLElement* node_array_info = require(node_array_type, "ArrayInfo");
            uint8_t lbound = requireNumber<uint8_t>(node_array_info, "LBound");
            if (lbound == 0)
            {
                entry.subindex = 1;
                entry.bitlen   = requireNumber<uint16_t>(node_array_type, "BitSize");
                object.entries.push_back(std::move(entry));
            }
            else
            {
                // uint16_t loop counter is required: with an 8-bit counter and
                // <Elements>255</Elements> (real ETG examples have this), the
                // post-increment wraps 255 -> 0 and the loop never terminates.
                uint16_t elements = requireNumber<uint16_t>(node_array_info, "Elements");
                if (elements > 0xFF)
                {
                    fail(node_array_info, "<Elements> > 255 exceeds CoE SubIndex space");
                }
                // An empty array (<Elements>0</Elements>) is degenerate but shipped
                // by real vendors; emit no element entries rather than rejecting.
                if (elements != 0)
                {
                    uint16_t total_bitlen   = requireNumber<uint16_t>(node_subitem, "BitSize");
                    uint16_t element_bitoff = requireNumber<uint16_t>(node_subitem, "BitOffs");
                    uint16_t count          = elements;
                    uint16_t element_bitlen;
                    uint16_t base_bitlen    = basicBitSize(array_type);
                    if (total_bitlen % elements == 0)
                    {
                        element_bitlen = static_cast<uint16_t>(total_bitlen / elements);
                    }
                    else if ((base_bitlen != 0) and (total_bitlen % base_bitlen == 0))
                    {
                        // SubItem <BitSize> may disagree with <ArrayInfo> Elements
                        // (Beckhoff EP6224: 64-bit SubItem on a 5-element UINT type):
                        // the base type size and the SubItem size are authoritative.
                        element_bitlen = base_bitlen;
                        count          = static_cast<uint16_t>(total_bitlen / element_bitlen);
                        if (count > 0xFF)
                        {
                            fail(node_subitem, "array element count exceeds CoE SubIndex space");
                        }
                    }
                    else
                    {
                        fail(node_subitem, "array <BitSize> " + std::to_string(total_bitlen)
                            + " not divisible by <Elements> " + std::to_string(elements));
                    }

                    for (uint16_t i = 1; i <= count; ++i)
                    {
                        // Use uint32_t for the intermediate offset arithmetic to
                        // detect the (degenerate but reachable) case where the
                        // final offset exceeds the uint16_t range.
                        uint32_t offset = static_cast<uint32_t>(element_bitoff)
                                        + static_cast<uint32_t>(element_bitlen) * (i - 1);
                        if (offset > 0xFFFF)
                        {
                            fail(node_subitem, "array element bitoff " + std::to_string(offset) + " exceeds uint16 range");
                        }
                        CoE::Entry e;
                        e.type        = entry.type;
                        e.access      = entry.access;
                        e.description = entry.description;
                        e.bitlen      = element_bitlen;
                        e.bitoff      = static_cast<uint16_t>(offset);
                        e.subindex    = static_cast<uint8_t>(i);
                        object.entries.push_back(std::move(e));
                    }
                }
            }
        }
    }

    XMLElement* node_info = node->FirstChildElement("Info");
    if (node_info == nullptr)
    {
        return object;
    }
    XMLElement* object_subitem = node_info->FirstChildElement("SubItem");
    for (auto& entry : object.entries)
    {
        if (object_subitem == nullptr)
        {
            break;
        }

        std::string name = optionalText(object_subitem, "Name");
        if (not name.empty())
        {
            entry.description = name;
        }
        loadDefaultData(object_subitem, object, entry);

        object_subitem = object_subitem->NextSiblingElement();
    }

    return object;
}

}
