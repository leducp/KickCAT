#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>

#include "kickcat/debug.h"
#include "kickcat/ESI/Parser.h"

using namespace tinyxml2;

namespace kickcat::ESI
{

const std::unordered_map<std::string, CoE::DataType> Parser::BASIC_TYPES
{
    {"BOOL",   CoE::DataType::BOOLEAN     },
    {"BYTE",   CoE::DataType::BYTE        },
    {"WORD",   CoE::DataType::WORD        },
    {"DWORD",  CoE::DataType::DWORD       },
    {"SINT",   CoE::DataType::INTEGER8    },
    {"INT",    CoE::DataType::INTEGER16   },
    {"INT24",  CoE::DataType::INTEGER24   },
    {"DINT",   CoE::DataType::INTEGER32   },
    {"INT40",  CoE::DataType::INTEGER40   },
    {"INT48",  CoE::DataType::INTEGER48   },
    {"INT56",  CoE::DataType::INTEGER56   },
    {"LINT",   CoE::DataType::INTEGER64   },
    {"USINT",  CoE::DataType::UNSIGNED8   },
    {"UINT",   CoE::DataType::UNSIGNED16  },
    {"UINT24", CoE::DataType::UNSIGNED24  },
    {"UDINT",  CoE::DataType::UNSIGNED32  },
    {"UINT40", CoE::DataType::UNSIGNED40  },
    {"UINT48", CoE::DataType::UNSIGNED48  },
    {"UINT56", CoE::DataType::UNSIGNED56  },
    {"ULINT",  CoE::DataType::UNSIGNED64  },
    {"REAL",   CoE::DataType::REAL32      },
    {"LREAL",  CoE::DataType::REAL64      },
    {"BIT2",   CoE::DataType::BIT2        },
    {"BIT3",   CoE::DataType::BIT3        },
    {"BIT4",   CoE::DataType::BIT4        },
    {"BIT5",   CoE::DataType::BIT5        },
    {"BIT6",   CoE::DataType::BIT6        },
    {"BIT7",   CoE::DataType::BIT7        },
    {"BIT8",   CoE::DataType::BIT8        },
};

namespace
{
    XMLElement* requireChild(XMLNode* node, char const* name)
    {
        auto element = node->FirstChildElement(name);
        if (element == nullptr)
        {
            std::string desc = "Cannot find child element <";
            desc += node->Value();
            desc += "> -> ";
            desc += name;
            throw std::invalid_argument(desc);
        }
        return element;
    }

    char const* textOrEmpty(XMLElement* node)
    {
        if (node == nullptr)
        {
            return "";
        }
        char const* text = node->GetText();
        if (text == nullptr)
        {
            return "";
        }
        return text;
    }

    // Throws when a mandatory text-bearing element is missing or has empty content.
    char const* requireText(XMLElement* elem, char const* child, std::string const& where)
    {
        if (elem == nullptr)
        {
            std::string what = "ESI: missing mandatory <";
            what += child;
            what += "> in ";
            what += where;
            throw std::invalid_argument(what);
        }
        char const* text = elem->GetText();
        if (text == nullptr)
        {
            std::string what = "ESI: empty <";
            what += child;
            what += "> in ";
            what += where;
            throw std::invalid_argument(what);
        }
        return text;
    }

    // Parent + child-name convenience: fetches the child element, then
    // delegates to requireText. Distinct name from requireText to avoid an
    // overload-resolution trap where a derived-class pointer (XMLElement*)
    // silently binds to the wrong overload.
    char const* requireChildText(XMLNode* parent, char const* child, std::string const& where)
    {
        return requireText(parent->FirstChildElement(child), child, where);
    }

    // Nullable read: returns nullptr if the child is missing or has no text.
    char const* findText(XMLNode* parent, char const* child)
    {
        auto* elem = parent->FirstChildElement(child);
        if (elem == nullptr)
        {
            return nullptr;
        }
        return elem->GetText();
    }

    int64_t parseHexDec(std::string text, std::string const& where = {})
    {
        auto fail = [&](char const* msg)
        {
            std::string what = "ESI: ";
            what += msg;
            what += " '";
            what += text;
            what += "'";
            if (not where.empty())
            {
                what += " in ";
                what += where;
            }
            throw std::invalid_argument(what);
        };

        if (text.empty())
        {
            fail("empty numeric value");
        }
        try
        {
            if (text.rfind("#x", 0) == 0)
            {
                if (text.size() == 2) { fail("'#x' with no hex digits"); }
                text[0] = '0';
                return std::stoll(text, nullptr, 16);
            }
            if (text.rfind("0x", 0) == 0 or text.rfind("0X", 0) == 0)
            {
                if (text.size() == 2) { fail("'0x' with no hex digits"); }
                return std::stoll(text, nullptr, 16);
            }
            return std::stoll(text, nullptr, 10);
        }
        catch (std::invalid_argument const&)
        {
            fail("invalid numeric value");
        }
        catch (std::out_of_range const&)
        {
            fail("numeric value exceeds int64 range");
        }
        return 0;  // unreachable; fail() always throws
    }

    // Narrow an int64_t to T with bounds checking. For signed T, also accepts
    // the unsigned bit pattern in [0, max-of-make_unsigned_t<T>] so that real
    // ETG ESIs using #xFFFFFFFF as a signed-int32 sentinel parse as -1 rather
    // than throwing.
    template<typename T>
    T narrowChecked(int64_t value, std::string const& text, std::string const& where)
    {
        static_assert(sizeof(T) <= sizeof(int64_t), "narrowChecked unsupported for T wider than int64");
        auto fail = [&](char const* kind)
        {
            std::string what = "ESI: value '";
            what += text;
            what += "' out of range for ";
            what += std::to_string(sizeof(T) * 8);
            what += "-bit ";
            what += kind;
            what += " type in ";
            what += where;
            throw std::invalid_argument(what);
        };

        if constexpr (std::is_signed_v<T>)
        {
            using U = std::make_unsigned_t<T>;
            int64_t lo = static_cast<int64_t>(std::numeric_limits<T>::min());
            int64_t hi = static_cast<int64_t>(std::numeric_limits<U>::max());
            if (value < lo or value > hi)
            {
                fail("signed");
            }
            return static_cast<T>(static_cast<U>(value));
        }
        else
        {
            int64_t hi = static_cast<int64_t>(std::numeric_limits<T>::max());
            if (value < 0 or value > hi)
            {
                fail("unsigned");
            }
            return static_cast<T>(value);
        }
    }

    template<typename T>
    T parseHexDec(std::string text, std::string const& where)
    {
        int64_t value = parseHexDec(text, where);
        return narrowChecked<T>(value, text, where);
    }

    template<typename T>
    T requireNumber(XMLNode* parent, char const* child, std::string const& where)
    {
        char const* text = requireChildText(parent, child, where);
        return parseHexDec<T>(text, where);
    }

    std::string objectLabel(uint16_t index)
    {
        char buf[24];
        std::snprintf(buf, sizeof(buf), "Object 0x%04x", index);
        return buf;
    }

    // xs:boolean per the ESI schema. Returns false when the attribute is
    // absent (schema default for the ESI attrs we read); throws when present
    // but not one of the four canonical values, so typos like Virtual="yes"
    // fail loudly instead of silently being treated as false.
    bool readBoolAttr(XMLElement* node, char const* name)
    {
        if (node == nullptr)
        {
            return false;
        }
        char const* raw = node->Attribute(name);
        if (raw == nullptr)
        {
            return false;
        }
        if (std::strcmp(raw, "true")  == 0 or std::strcmp(raw, "1") == 0) { return true;  }
        if (std::strcmp(raw, "false") == 0 or std::strcmp(raw, "0") == 0) { return false; }

        std::string what = "ESI: attribute '";
        what += name;
        what += "' is not a valid xs:boolean (got '";
        what += raw;
        what += "')";
        throw std::invalid_argument(what);
    }

    // xs:int attribute: signed decimal, no #x hex prefix. nullopt when truly
    // absent; throws when present but not a valid xs:int (e.g. Chn="abc"),
    // so callers can use .value_or(default) for the schema-default case
    // without silently swallowing malformed input.
    std::optional<int32_t> readIntAttr(XMLElement* node, char const* name)
    {
        if (node == nullptr)
        {
            return std::nullopt;
        }
        int32_t value = 0;
        auto result = node->QueryIntAttribute(name, &value);
        if (result == tinyxml2::XML_NO_ATTRIBUTE)
        {
            return std::nullopt;
        }
        if (result != tinyxml2::XML_SUCCESS)
        {
            std::string what = "ESI: attribute '";
            what += name;
            what += "' is not a valid xs:int";
            throw std::invalid_argument(what);
        }
        return value;
    }

    std::vector<transition::Type> parseTransitions(XMLElement* parent)
    {
        std::vector<transition::Type> out;
        for (auto* t = parent->FirstChildElement("Transition"); t != nullptr; t = t->NextSiblingElement("Transition"))
        {
            char const* text = t->GetText();
            if (text == nullptr)
            {
                throw std::invalid_argument("ESI: empty <Transition>");
            }
            transition::Type type;
            fromString(text, type);
            out.push_back(type);
        }
        if (out.empty())
        {
            throw std::invalid_argument("ESI: InitCmd has no <Transition> child (schema requires at least one)");
        }
        return out;
    }

    std::string commentOf(XMLElement* parent)
    {
        auto* c = parent->FirstChildElement("Comment");
        if (c == nullptr)
        {
            return {};
        }
        char const* text = c->GetText();
        if (text == nullptr)
        {
            return {};
        }
        return text;
    }
}

std::optional<uint32_t> Parser::readHexDecAttr(XMLElement* node, char const* name)
{
    if (node == nullptr)
    {
        return std::nullopt;
    }
    char const* raw = node->Attribute(name);
    if (raw == nullptr)
    {
        return std::nullopt;
    }
    return parseHexDec<uint32_t>(raw, std::string{"@"} + name);
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
    vendor_xml_ = requireChild(root_, "Vendor");
    auto desc   = requireChild(root_, "Descriptions");
    devices_    = requireChild(desc,  "Devices");

    auto vendor_name = vendor_xml_->FirstChildElement("Name");
    vendor_name_ = textOrEmpty(vendor_name);
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
    std::vector<DeviceSummary> out;
    for (auto* d = devices_->FirstChildElement("Device"); d != nullptr; d = d->NextSiblingElement("Device"))
    {
        out.push_back(summarize(d));
    }
    return out;
}

DeviceSummary Parser::summarize(XMLElement* device)
{
    DeviceSummary s;
    auto type_node = device->FirstChildElement("Type");
    s.type = textOrEmpty(type_node);
    s.product_code = readHexDecAttr(type_node, "ProductCode").value_or(0);
    s.revision_no  = readHexDecAttr(type_node, "RevisionNo" ).value_or(0);
    s.serial_no    = readHexDecAttr(type_node, "SerialNo"   ).value_or(0);
    s.name = textOrEmpty(device->FirstChildElement("Name"));
    return s;
}

XMLElement* Parser::selectDevice(DeviceFilter const& filter)
{
    bool any_filter = filter.type or filter.product_code or filter.revision_no;

    if (not any_filter)
    {
        std::size_t i = 0;
        for (auto* d = devices_->FirstChildElement("Device"); d != nullptr; d = d->NextSiblingElement("Device"))
        {
            if (i == filter.index)
            {
                return d;
            }
            ++i;
        }
        throw std::invalid_argument("ESI: device index out of range");
    }

    for (auto* d = devices_->FirstChildElement("Device"); d != nullptr; d = d->NextSiblingElement("Device"))
    {
        auto* type_node = d->FirstChildElement("Type");
        if (filter.type)
        {
            std::string type_text = textOrEmpty(type_node);
            if (type_text != *filter.type)
            {
                continue;
            }
        }
        if (filter.product_code)
        {
            auto pc = readHexDecAttr(type_node, "ProductCode");
            if (not pc or *pc != *filter.product_code)
            {
                continue;
            }
        }
        if (filter.revision_no)
        {
            auto rev = readHexDecAttr(type_node, "RevisionNo");
            if (not rev or *rev != *filter.revision_no)
            {
                continue;
            }
        }
        return d;
    }

    throw std::invalid_argument("ESI: no device matches filter");
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
    auto* device_node = selectDevice(filter);

    Device device;
    device.vendor_name = vendor_name_;
    // <Vendor>/<Id> is mandatory per ETG.2000 in spirit, but real vendor ESI
    // files (notably some catalog placeholders) ship with <Id></Id> empty.
    // Require the element to be present; allow empty text -> vendor_id = 0
    // to preserve compatibility with those files.
    auto* id_node = requireChild(vendor_xml_, "Id");
    char const* id_text = id_node->GetText();
    if (id_text != nullptr and *id_text != '\0')
    {
        device.vendor_id = parseHexDec<uint32_t>(id_text, "Vendor/Id");
    }

    auto* type_node = device_node->FirstChildElement("Type");
    device.type         = textOrEmpty(type_node);
    device.product_code = readHexDecAttr(type_node, "ProductCode").value_or(0);
    device.revision_no  = readHexDecAttr(type_node, "RevisionNo" ).value_or(0);
    device.serial_no    = readHexDecAttr(type_node, "SerialNo"   ).value_or(0);
    device.name         = textOrEmpty(device_node->FirstChildElement("Name"));
    device.group_type   = textOrEmpty(device_node->FirstChildElement("GroupType"));

    // <Profile> is optional in real-world ESIs — Beckhoff IO terminal entries
    // and other simple slaves have no CoE dictionary. Accept devices without
    // <Profile>; the dictionary will only contain the synthesised 0x1C00/PDO
    // mapping objects derived from <Sm>/<Pdo> declarations.
    auto* profile_node = device_node->FirstChildElement("Profile");
    if (profile_node != nullptr)
    {
        auto* profile_no = profile_node->FirstChildElement("ProfileNo");
        profile_no_ = textOrEmpty(profile_no);
        if (not profile_no_.empty())
        {
            device.profile_no = parseHexDec<uint16_t>(profile_no_, "Profile/ProfileNo");
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
    synthesizePdoMappingObjects(device);
    return device;
}

namespace transition
{
    char const* toString(Type const& t)
    {
        switch (t)
        {
            case IP: { return "IP"; }
            case PS: { return "PS"; }
            case SO: { return "SO"; }
            case SP: { return "SP"; }
            case OP: { return "OP"; }
            case OS: { return "OS"; }
            default: { return "unknown"; }
        }
    }

    void fromString(std::string_view text, Type& out)
    {
        if (text == "IP") { out = IP; return; }
        if (text == "PS") { out = PS; return; }
        if (text == "SO") { out = SO; return; }
        if (text == "SP") { out = SP; return; }
        if (text == "OP") { out = OP; return; }
        if (text == "OS") { out = OS; return; }

        std::string what = "ESI: unknown Transition '";
        what.append(text);
        what += "'";
        throw std::invalid_argument(what);
    }
}

void Parser::parseMailbox(XMLElement* device, std::optional<Mailbox>& out)
{
    // The <Device>/<Mailbox> block is distinct from <Device>/<Info>/<Mailbox>
    // (which carries request/response timeouts). Iterate children only — never
    // pick the one nested under <Info>.
    auto* mbx = device->FirstChildElement("Mailbox");
    if (mbx == nullptr)
    {
        return;
    }

    out.emplace();
    out->data_link_layer = readBoolAttr(mbx, "DataLinkLayer");
    out->real_time_mode  = readBoolAttr(mbx, "RealTimeMode");

    if (auto* coe = mbx->FirstChildElement("CoE"))
    {
        Mailbox::CoE block;
        block.sdo_info                   = readBoolAttr(coe, "SdoInfo");
        block.pdo_assign                 = readBoolAttr(coe, "PdoAssign");
        block.pdo_config                 = readBoolAttr(coe, "PdoConfig");
        block.pdo_upload                 = readBoolAttr(coe, "PdoUpload");
        block.complete_access            = readBoolAttr(coe, "CompleteAccess");
        block.segmented_sdo              = readBoolAttr(coe, "SegmentedSdo");
        block.diag_history               = readBoolAttr(coe, "DiagHistory");
        block.sdo_upload_with_max_length = readBoolAttr(coe, "SdoUploadWithMaxLength");
        block.time_distribution          = readBoolAttr(coe, "TimeDistribution");
        if (char const* eds = coe->Attribute("EdsFile"))
        {
            block.eds_file = eds;
        }

        // CoE/Object (obsolete in the XSD) is ignored; modern ESIs use <InitCmd>.
        for (auto* ic = coe->FirstChildElement("InitCmd"); ic != nullptr; ic = ic->NextSiblingElement("InitCmd"))
        {
            Mailbox::CoE::InitCmd cmd;
            cmd.transitions = parseTransitions(ic);
            cmd.index    = requireNumber<uint16_t>(ic, "Index",    "Mailbox/CoE/InitCmd");
            cmd.subindex = requireNumber<uint8_t> (ic, "SubIndex", "Mailbox/CoE/InitCmd");
            auto* data = requireChild(ic, "Data");
            cmd.data = loadHexBinary(data);
            cmd.adapt_automatically   = readBoolAttr(data, "AdaptAutomatically");
            cmd.complete_access       = readBoolAttr(ic, "CompleteAccess");
            cmd.overwritten_by_module = readBoolAttr(ic, "OverwrittenByModule");
            cmd.comment = commentOf(ic);
            block.init_cmds.push_back(std::move(cmd));
        }
        out->coe = std::move(block);
    }

    if (auto* eoe = mbx->FirstChildElement("EoE"))
    {
        Mailbox::EoE block;
        block.ip         = readBoolAttr(eoe, "IP");
        block.mac        = readBoolAttr(eoe, "MAC");
        block.time_stamp = readBoolAttr(eoe, "TimeStamp");

        for (auto* ic = eoe->FirstChildElement("InitCmd"); ic != nullptr; ic = ic->NextSiblingElement("InitCmd"))
        {
            Mailbox::EoE::InitCmd cmd;
            cmd.transitions = parseTransitions(ic);
            cmd.type = requireNumber<int32_t>(ic, "Type", "Mailbox/EoE/InitCmd");
            cmd.data = loadHexBinary(requireChild(ic, "Data"));
            cmd.comment = commentOf(ic);
            block.init_cmds.push_back(std::move(cmd));
        }
        out->eoe = std::move(block);
    }

    if (mbx->FirstChildElement("FoE") != nullptr)
    {
        out->foe = Mailbox::FoE{};
    }

    if (auto* soe = mbx->FirstChildElement("SoE"))
    {
        Mailbox::SoE block;
        block.channel_count      = readIntAttr(soe, "ChannelCount");
        block.drive_follows_bit3 = readBoolAttr(soe, "DriveFollowsBit3Support");

        for (auto* ic = soe->FirstChildElement("InitCmd"); ic != nullptr; ic = ic->NextSiblingElement("InitCmd"))
        {
            Mailbox::SoE::InitCmd cmd;
            cmd.transitions = parseTransitions(ic);
            cmd.idn = requireNumber<int32_t>(ic, "IDN", "Mailbox/SoE/InitCmd");
            cmd.channel = readIntAttr(ic, "Chn").value_or(0);
            cmd.data = loadHexBinary(requireChild(ic, "Data"));
            cmd.comment = commentOf(ic);
            block.init_cmds.push_back(std::move(cmd));
        }
        out->soe = std::move(block);
    }

    if (auto* aoe = mbx->FirstChildElement("AoE"))
    {
        Mailbox::AoE block;
        block.ads_router            = readBoolAttr(aoe, "AdsRouter");
        block.generate_own_net_id   = readBoolAttr(aoe, "GenerateOwnNetId");
        block.initialize_own_net_id = readBoolAttr(aoe, "InitializeOwnNetId");

        for (auto* ic = aoe->FirstChildElement("InitCmd"); ic != nullptr; ic = ic->NextSiblingElement("InitCmd"))
        {
            Mailbox::AoE::InitCmd cmd;
            cmd.transitions = parseTransitions(ic);
            cmd.data = loadHexBinary(requireChild(ic, "Data"));
            cmd.comment = commentOf(ic);
            block.init_cmds.push_back(std::move(cmd));
        }
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
    PdoEntry parsePdoEntry(XMLElement* node, std::string const& where)
    {
        PdoEntry entry;
        entry.index   = requireNumber<uint16_t>(node, "Index", where);
        // SubIndex is optional per the XSD (minOccurs=0); padding entries use 0.
        if (char const* sub = findText(node, "SubIndex"))
        {
            entry.subindex = parseHexDec<uint8_t>(sub, where + " SubIndex");
        }
        entry.bit_len = requireNumber<uint16_t>(node, "BitLen", where);

        if (char const* name = findText(node, "Name"))
        {
            entry.name = name;
        }
        if (char const* comment = findText(node, "Comment"))
        {
            entry.comment = comment;
        }
        if (char const* data_type = findText(node, "DataType"))
        {
            entry.data_type = data_type;
        }

        entry.fixed              = readBoolAttr(node, "Fixed");
        entry.safety_conn_number = readIntAttr (node, "SafetyConnNumber");
        if (char const* type = node->Attribute("SafetyPdoEntryType"))
        {
            entry.safety_pdo_entry_type = type;
        }
        return entry;
    }
}

void Parser::parsePdos(XMLElement* device, char const* element_name, std::vector<Pdo>& out)
{
    for (auto* pdo_node = device->FirstChildElement(element_name); pdo_node != nullptr;
         pdo_node       = pdo_node->NextSiblingElement(element_name))
    {
        Pdo pdo;

        std::string where = element_name;
        // <Index>/@DependOnSlot/@DependOnSlotGroup are modular-device attributes
        // and are not read.
        pdo.index = requireNumber<uint16_t>(pdo_node, "Index", where);

        char buf[40];
        std::snprintf(buf, sizeof(buf), "%s 0x%04x", element_name, pdo.index);
        where = buf;

        // <Name> is mandatory per PdoType. Multi-language variants (@LcId) are
        // not distinguished; the first <Name> wins.
        pdo.name = requireChildText(pdo_node, "Name", where);

        pdo.sm                    = readIntAttr (pdo_node, "Sm");
        pdo.su                    = readIntAttr (pdo_node, "Su");
        pdo.fixed                 = readBoolAttr(pdo_node, "Fixed");
        pdo.mandatory             = readBoolAttr(pdo_node, "Mandatory");
        pdo.is_virtual            = readBoolAttr(pdo_node, "Virtual");
        pdo.os_fac                = readIntAttr (pdo_node, "OSFac");
        pdo.os_min                = readIntAttr (pdo_node, "OSMin");
        pdo.os_max                = readIntAttr (pdo_node, "OSMax");
        pdo.os_index_inc          = readIntAttr (pdo_node, "OSIndexInc");
        pdo.pdo_order             = readIntAttr (pdo_node, "PdoOrder");
        pdo.overwritten_by_module = readBoolAttr(pdo_node, "OverwrittenByModule");
        pdo.sra_parameter         = readBoolAttr(pdo_node, "SRA_Parameter");
        pdo.safety_conn_number    = readIntAttr (pdo_node, "SafetyConnNumber");
        if (char const* type = pdo_node->Attribute("SafetyPdoType"))
        {
            pdo.safety_pdo_type = type;
        }

        for (auto* ex = pdo_node->FirstChildElement("Exclude"); ex != nullptr; ex = ex->NextSiblingElement("Exclude"))
        {
            char const* text = ex->GetText();
            if (text == nullptr)
            {
                throw std::invalid_argument("ESI: empty <Exclude> in " + where);
            }
            pdo.exclude.push_back(parseHexDec<uint16_t>(text, where + " Exclude"));
        }
        for (auto* ex_sm = pdo_node->FirstChildElement("ExcludedSm"); ex_sm != nullptr; ex_sm = ex_sm->NextSiblingElement("ExcludedSm"))
        {
            char const* text = ex_sm->GetText();
            if (text == nullptr)
            {
                throw std::invalid_argument("ESI: empty <ExcludedSm> in " + where);
            }
            pdo.excluded_sm.push_back(parseHexDec<int32_t>(text, where + " ExcludedSm"));
        }
        for (auto* entry = pdo_node->FirstChildElement("Entry"); entry != nullptr; entry = entry->NextSiblingElement("Entry"))
        {
            pdo.entries.push_back(parsePdoEntry(entry, where + " Entry"));
        }

        for (auto const& existing : out)
        {
            if (existing.index == pdo.index)
            {
                char buf[64];
                std::snprintf(buf, sizeof(buf), "ESI: duplicate %s <Index> 0x%04x", element_name, pdo.index);
                throw std::invalid_argument(buf);
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
        throw std::invalid_argument("ESI: PDO has more than 255 entries (cannot synthesize mapping)");
    }

    CoE::Entry subindex0{0, 8, 0, CoE::Access::READ, CoE::DataType::UNSIGNED8, "Number of entries"};
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
            throw std::invalid_argument("ESI: PDO entry BitLen > 255 cannot fit in mapping word");
        }
        CoE::Entry entry;
        entry.subindex    = static_cast<uint8_t>(i + 1);
        entry.bitlen      = 32;
        entry.bitoff      = bitoff;
        entry.access      = CoE::Access::READ;
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
        uint32_t packed = (static_cast<uint32_t>(e.index) << 16)
                        | (static_cast<uint32_t>(e.subindex) << 8)
                        | static_cast<uint32_t>(e.bit_len);
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
        throw std::invalid_argument("ESI: more than 255 PDOs assigned to a SyncManager");
    }

    CoE::Entry subindex0{0, 8, 0, CoE::Access::READ, CoE::DataType::UNSIGNED8, "Number of assigned PDOs"};
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
        entry.access      = CoE::Access::READ;
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

void Parser::synthesizePdoMappingObjects(Device& device)
{
    // Per-PDO mapping objects (0x16xx, 0x1Axx) — only add when the slave's
    // <Dictionary> doesn't already declare them explicitly. Explicit wins.
    for (auto const& pdo : device.rx_pdos)
    {
        if (not dictionaryContains(device.dictionary, pdo.index))
        {
            device.dictionary.push_back(buildMappingObject(pdo, true));
        }
    }
    for (auto const& pdo : device.tx_pdos)
    {
        if (not dictionaryContains(device.dictionary, pdo.index))
        {
            device.dictionary.push_back(buildMappingObject(pdo, false));
        }
    }

    // SM assignment objects 0x1C12 (RxPDO) and 0x1C13 (TxPDO). Per ETG.1000.6
    // these list the indexes of mapping objects assigned to the master->slave
    // and slave->master SyncManagers respectively. NOTE: we lump ALL rx_pdos
    // under 0x1C12 and ALL tx_pdos under 0x1C13 regardless of each Pdo's @Sm
    // attribute. Modular devices using non-default SMs (e.g. Sm=4 for RxPDO)
    // are misrepresented by this synthesis — the per-SM correct form would be
    // one assignment object per unique pdo.sm value at 0x1C10 + sm_index.
    if (not device.rx_pdos.empty() and not dictionaryContains(device.dictionary, 0x1C12))
    {
        device.dictionary.push_back(buildAssignmentObject(device.rx_pdos, 0x1C12, true));
    }
    if (not device.tx_pdos.empty() and not dictionaryContains(device.dictionary, 0x1C13))
    {
        device.dictionary.push_back(buildAssignmentObject(device.tx_pdos, 0x1C13, false));
    }
}

void Parser::parseEeprom(XMLElement* device, std::optional<Eeprom>& out)
{
    auto* eep = device->FirstChildElement("Eeprom");
    if (eep == nullptr)
    {
        return;
    }

    Eeprom block;
    block.assign_to_pdi = readBoolAttr(eep, "AssignToPdi");

    // EepromType is an xs:choice between raw <Data> and the structured form
    // (<ByteSize>+<ConfigData>+…). Reject documents carrying both so the error
    // names the real problem.
    auto* data_raw   = eep->FirstChildElement("Data");
    auto* byte_size  = eep->FirstChildElement("ByteSize");
    if (data_raw != nullptr and byte_size != nullptr)
    {
        throw std::invalid_argument(
            "ESI: <Eeprom> has both <Data> (raw form) and <ByteSize> (structured form); "
            "they are mutually exclusive per the schema");
    }
    if (data_raw != nullptr)
    {
        block.raw_data = loadHexBinary(data_raw);
        out = std::move(block);
        return;
    }

    // ByteSize and ConfigData are mandatory children of the structured form.
    block.byte_size   = parseHexDec<int32_t>(requireChildText(eep, "ByteSize", "Eeprom"), "Eeprom/ByteSize");
    block.config_data = loadHexBinary(requireChild(eep, "ConfigData"));

    if (auto* cfg2 = eep->FirstChildElement("ConfigData2"))
    {
        block.config_data2 = loadHexBinary(cfg2);
    }
    if (auto* boot = eep->FirstChildElement("BootStrap"))
    {
        block.bootstrap = loadHexBinary(boot);
    }
    for (auto* cat = eep->FirstChildElement("Category"); cat != nullptr; cat = cat->NextSiblingElement("Category"))
    {
        Eeprom::Category c;
        c.cat_no = requireNumber<int32_t>(cat, "CatNo", "Eeprom/Category");
        c.preserve_online_data = readBoolAttr(cat, "PreserveOnlineData");
        // Payload is an xs:choice; pick by element presence, not text content,
        // so an empty <DataString/> reads as an empty string rather than absent.
        // The numeric forms still require a value.
        if (auto* d = cat->FirstChildElement("Data"))
        {
            c.data = loadHexBinary(d);
        }
        else if (auto* s = cat->FirstChildElement("DataString"))
        {
            char const* text = s->GetText();
            if (text != nullptr)
            {
                c.data_string = text;
            }
            else
            {
                c.data_string.emplace();
            }
        }
        else if (cat->FirstChildElement("DataUINT") != nullptr)
        {
            c.data_uint = parseHexDec<int32_t>(requireChildText(cat, "DataUINT", "Eeprom/Category"), "Eeprom/Category/DataUINT");
        }
        else if (cat->FirstChildElement("DataUDINT") != nullptr)
        {
            c.data_udint = parseHexDec<int32_t>(requireChildText(cat, "DataUDINT", "Eeprom/Category"), "Eeprom/Category/DataUDINT");
        }
        else
        {
            throw std::invalid_argument("ESI: <Category> in Eeprom/Category missing payload (expected one of Data/DataString/DataUINT/DataUDINT)");
        }
        block.categories.push_back(std::move(c));
    }

    out = std::move(block);
}

namespace
{
    std::optional<OpMode::SyncTime> parseSyncTime(XMLElement* parent, char const* name)
    {
        auto* node = parent->FirstChildElement(name);
        if (node == nullptr)
        {
            return std::nullopt;
        }
        OpMode::SyncTime st;
        char const* text = node->GetText();
        if (text != nullptr)
        {
            st.value = parseHexDec<int32_t>(text, std::string{"Dc/OpMode/"} + name);
        }
        st.factor = readIntAttr(node, "Factor");
        return st;
    }

    std::optional<OpMode::ShiftTime> parseShiftTime(XMLElement* parent, char const* name)
    {
        auto* node = parent->FirstChildElement(name);
        if (node == nullptr)
        {
            return std::nullopt;
        }
        OpMode::ShiftTime st;
        char const* text = node->GetText();
        if (text != nullptr)
        {
            st.value = parseHexDec<int32_t>(text, std::string{"Dc/OpMode/"} + name);
        }
        st.factor             = readIntAttr(node, "Factor");
        if (node->Attribute("Input") != nullptr)
        {
            st.input = readBoolAttr(node, "Input");
        }
        st.output_delay_time  = readIntAttr(node, "OutputDelayTime");
        st.input_delay_time   = readIntAttr(node, "InputDelayTime");
        return st;
    }
}

void Parser::parseDc(XMLElement* device, std::optional<Dc>& out)
{
    auto* dc = device->FirstChildElement("Dc");
    if (dc == nullptr)
    {
        return;
    }

    Dc block;
    block.unknown_frmw              = readBoolAttr(dc, "UnknownFRMW");
    block.unknown_64bit             = readBoolAttr(dc, "Unknown64Bit");
    block.external_ref_clock        = readBoolAttr(dc, "ExternalRefClock");
    block.potential_reference_clock = readBoolAttr(dc, "PotentialReferenceClock");
    block.time_loop_control_only    = readBoolAttr(dc, "TimeLoopControlOnly");
    block.pdo_oversampling          = readBoolAttr(dc, "PdoOversampling");

    static char const* const CYCLE_NAMES[4] = {"CycleTimeSync0", "CycleTimeSync1", "CycleTimeSync2", "CycleTimeSync3"};
    static char const* const SHIFT_NAMES[4] = {"ShiftTimeSync0", "ShiftTimeSync1", "ShiftTimeSync2", "ShiftTimeSync3"};

    for (auto* om = dc->FirstChildElement("OpMode"); om != nullptr; om = om->NextSiblingElement("OpMode"))
    {
        OpMode mode;
        mode.name = requireChildText(om, "Name", "Dc/OpMode");
        if (char const* desc = findText(om, "Desc"))
        {
            mode.desc = desc;
        }
        mode.assign_activate = requireNumber<uint32_t>(om, "AssignActivate", "Dc/OpMode");
        if (char const* aa = findText(om, "ActivateAdditional"))
        {
            mode.activate_additional = parseHexDec<uint32_t>(aa, "Dc/OpMode/ActivateAdditional");
        }
        for (int i = 0; i < 4; ++i)
        {
            mode.cycle_time[i] = parseSyncTime (om, CYCLE_NAMES[i]);
            mode.shift_time[i] = parseShiftTime(om, SHIFT_NAMES[i]);
        }

        // <OpMode>/<Sm No="..">: @No plus the <Pdo>/@OSFac oversampling map. The
        // SyncType/CycleTime/ShiftTime children are obsolete in the XSD; skipped.
        for (auto* sm = om->FirstChildElement("Sm"); sm != nullptr; sm = sm->NextSiblingElement("Sm"))
        {
            OpMode::SmConfig cfg;
            auto no = readIntAttr(sm, "No");
            if (not no)
            {
                throw std::invalid_argument("ESI: <Sm> in Dc/OpMode missing required @No");
            }
            cfg.no = *no;

            for (auto* p = sm->FirstChildElement("Pdo"); p != nullptr; p = p->NextSiblingElement("Pdo"))
            {
                char const* text = p->GetText();
                if (text == nullptr)
                {
                    throw std::invalid_argument("ESI: empty <Pdo> in Dc/OpMode/Sm");
                }
                OpMode::SmConfig::PdoRef ref;
                ref.index  = parseHexDec<uint16_t>(text, "Dc/OpMode/Sm/Pdo");
                ref.os_fac = readIntAttr(p, "OSFac");
                cfg.pdos.push_back(ref);
            }
            mode.sm_configs.push_back(std::move(cfg));
        }
        block.op_modes.push_back(std::move(mode));
    }

    out = std::move(block);
}

void Parser::parseSyncManagers(XMLElement* device, std::vector<SmInfo>& out)
{
    for (auto* sm = device->FirstChildElement("Sm"); sm != nullptr; sm = sm->NextSiblingElement("Sm"))
    {
        SmInfo entry;

        char const* text = sm->GetText();
        if (text != nullptr)
        {
            fromString(text, entry.type);
        }

        entry.min_size      = static_cast<uint16_t>(readHexDecAttr(sm, "MinSize"     ).value_or(0));
        entry.max_size      = static_cast<uint16_t>(readHexDecAttr(sm, "MaxSize"     ).value_or(0));
        entry.default_size  = static_cast<uint16_t>(readHexDecAttr(sm, "DefaultSize" ).value_or(0));
        entry.start_address = static_cast<uint16_t>(readHexDecAttr(sm, "StartAddress").value_or(0));
        entry.control_byte  = static_cast<uint8_t> (readHexDecAttr(sm, "ControlByte" ).value_or(0));
        entry.enable        = static_cast<uint8_t> (readHexDecAttr(sm, "Enable"      ).value_or(0));
        entry.is_virtual    = readBoolAttr(sm, "Virtual");
        entry.op_only       = readBoolAttr(sm, "OpOnly");

        out.push_back(entry);
    }
}

void Parser::parseSyncUnits(XMLElement* device, std::vector<SyncUnit>& out)
{
    for (auto* su = device->FirstChildElement("Su"); su != nullptr; su = su->NextSiblingElement("Su"))
    {
        SyncUnit entry;
        entry.separate_su          = readBoolAttr(su, "SeparateSu");
        entry.separate_frame       = readBoolAttr(su, "SeparateFrame");
        entry.frame_repeat_support = readBoolAttr(su, "FrameRepeatSupport");
        out.push_back(entry);
    }
}

void Parser::parseFmmus(XMLElement* device, std::vector<Fmmu>& out)
{
    for (auto* fmmu = device->FirstChildElement("Fmmu"); fmmu != nullptr; fmmu = fmmu->NextSiblingElement("Fmmu"))
    {
        Fmmu entry;

        char const* text = fmmu->GetText();
        if (text != nullptr)
        {
            fromString(text, entry.type);
        }

        if (auto sm_attr = readHexDecAttr(fmmu, "Sm"))
        {
            entry.sm = static_cast<int>(*sm_attr);
        }
        if (auto su_attr = readHexDecAttr(fmmu, "Su"))
        {
            entry.su = static_cast<int>(*su_attr);
        }
        entry.op_only = readBoolAttr(fmmu, "OpOnly");

        out.push_back(entry);
    }
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
        dtypes_       = dictionary->FirstChildElement("DataTypes");
        // <Dictionary> itself is optional, but when present the XSD requires an
        // <Objects> child (DictionaryType sequence).
        auto* objects = requireChild(dictionary, "Objects");
        for (auto* node_object = objects->FirstChildElement(); node_object != nullptr; node_object = node_object->NextSiblingElement())
        {
            out.push_back(createObject(node_object));
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
        throw std::invalid_argument("ESI: more than 255 <Sm> entries cannot fit in 0x1C00 SubIndex space");
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

std::vector<uint8_t> Parser::loadHexBinary(XMLElement* node)
{
    if (node == nullptr)
    {
        return {};
    }
    char const* raw = node->GetText();
    if (raw == nullptr)
    {
        return {};
    }
    std::string field = raw;
    if (field.size() % 2 != 0)
    {
        throw std::invalid_argument("ESI: hex binary <" + std::string{node->Value()}
            + "> has odd length (" + std::to_string(field.size()) + " chars)");
    }
    std::vector<uint8_t> data;
    data.reserve(field.size() / 2);

    for (std::size_t i = 0; i < field.size(); i += 2)
    {
        char buf[3] = {field[i], field[i + 1], '\0'};
        char* end = nullptr;
        unsigned long byte = std::strtoul(buf, &end, 16);
        if (end != buf + 2)
        {
            std::string what = "ESI: hex binary <";
            what += node->Value();
            what += "> contains non-hex pair '";
            what += buf;
            what += "' at byte ";
            what += std::to_string(i / 2);
            throw std::invalid_argument(what);
        }
        data.push_back(static_cast<uint8_t>(byte));
    }

    return data;
}


void Parser::loadDefaultData(XMLNode* node, CoE::Object& obj, CoE::Entry& entry)
{
    auto node_info = node->FirstChildElement("Info");
    if (node_info == nullptr)
    {
        return;
    }

    auto node_default_data = node_info->FirstChildElement("DefaultData");
    if (node_default_data != nullptr)
    {
        // VISIBLE_STRING and other binary defaults share the same loader; the
        // ESI <DefaultData> hex-binary content is already in the natural byte
        // order (ASCII bytes for strings, little-endian for numerics).
        std::vector<uint8_t> data = loadHexBinary(node_default_data);

        if (data.size() != (entry.bitlen / 8))
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

    if (char const* default_value = findText(node_info, "DefaultValue"))
    {
        uint32_t size = entry.bitlen / 8;
        if (size == 0)
        {
            return;
        }
        int64_t value = parseHexDec(default_value, objectLabel(obj.index) + " DefaultValue");
        uint32_t copy_size = std::min<uint32_t>(size, sizeof(int64_t));
        entry.data = std::malloc(size);
        if (copy_size < size)
        {
            std::memset(entry.data, 0, size);
        }
        std::memcpy(entry.data, &value, copy_size);
    }
}

uint16_t Parser::loadAccess(XMLNode* node)
{
    uint16_t flags = 0;

    auto node_flags = node->FirstChildElement("Flags");
    if (node_flags == nullptr)
    {
        return flags;
    }

    auto node_access = node_flags->FirstChildElement("Access");
    if (node_access != nullptr)
    {
        char const* raw = node_access->GetText();
        std::string access;
        if (raw != nullptr)
        {
            access = raw;
        }

        if (access == "rw" or access == "ro")
        {
            flags |= CoE::Access::READ;
        }
        if (access == "rw" or access == "wo")
        {
            flags |= CoE::Access::WRITE;
        }

        auto parseRestrictions = [](char const* raw_restrictions) -> uint16_t
        {
            if (raw_restrictions == nullptr)
            {
                return CoE::Access::READ;
            }

            std::string restrictions{raw_restrictions};
            std::transform(restrictions.begin(), restrictions.end(), restrictions.begin(),
                [](char c){ return std::tolower(c); });

            uint16_t result = 0;
            if (restrictions.find("preop")  != std::string::npos) { result |= CoE::Access::READ_PREOP;  }
            if (restrictions.find("safeop") != std::string::npos) { result |= CoE::Access::READ_SAFEOP; }
            if (restrictions.find("_op")    != std::string::npos) { result |= CoE::Access::READ_OP;     }
            if (restrictions.find("op") == 0)                     { result |= CoE::Access::READ_OP;     }

            return result;
        };

        uint16_t restrictions_mask = 0;
        restrictions_mask |= (parseRestrictions(node_access->Attribute("ReadRestrictions"))  << 0);
        restrictions_mask |= (parseRestrictions(node_access->Attribute("WriteRestrictions")) << 3);

        flags &= restrictions_mask;
    }
    else
    {
        flags |= CoE::Access::READ;
    }

    if (char const* mapping = findText(node_flags, "PdoMapping"))
    {
        for (char const* c = mapping; *c != '\0'; ++c)
        {
            if (std::tolower(*c) == 'r')
            {
                flags |= CoE::Access::RxPDO;
            }
            if (std::tolower(*c) == 't')
            {
                flags |= CoE::Access::TxPDO;
            }
        }
    }

    if (char const* t = findText(node_flags, "Backup"); t and t[0] == '1')
    {
        flags |= CoE::Access::BACKUP;
    }
    if (char const* t = findText(node_flags, "Setting"); t and t[0] == '1')
    {
        flags |= CoE::Access::SETTING;
    }

    return flags;
}

CoE::DataType Parser::resolveType(std::string const& type_name, int depth)
{
    if (depth > MAX_TYPE_DEPTH)
    {
        std::string what = "ESI: <BaseType> recursion exceeds depth ";
        what += std::to_string(MAX_TYPE_DEPTH);
        what += " resolving '";
        what += type_name;
        what += "' (cycle?)";
        throw std::invalid_argument(what);
    }

    auto it = BASIC_TYPES.find(type_name);
    if (it != BASIC_TYPES.end())
    {
        return it->second;
    }

    if (type_name.find("STRING") != std::string::npos)
    {
        return CoE::DataType::VISIBLE_STRING;
    }

    if (dtypes_ == nullptr)
    {
        return CoE::DataType::UNKNOWN;
    }
    auto dtype = dtypes_->FirstChildElement();
    while (dtype)
    {
        auto name_elem = dtype->FirstChildElement("Name");
        char const* name_text = nullptr;
        if (name_elem != nullptr)
        {
            name_text = name_elem->GetText();
        }
        if (name_text != nullptr and type_name == name_text)
        {
            if (dtype->FirstChildElement("SubItem") or dtype->FirstChildElement("ArrayInfo"))
            {
                return CoE::DataType::UNKNOWN;
            }

            auto base = dtype->FirstChildElement("BaseType");
            if (base)
            {
                char const* base_text = base->GetText();
                if (base_text == nullptr)
                {
                    std::string what = "ESI: empty <BaseType> for DataType '";
                    what += type_name;
                    what += "'";
                    throw std::invalid_argument(what);
                }
                return resolveType(base_text, depth + 1);
            }

            break;
        }
        dtype = dtype->NextSiblingElement();
    }

    return CoE::DataType::UNKNOWN;
}

std::tuple<CoE::DataType, uint16_t, uint16_t> Parser::parseType(XMLNode* node)
{
    char const* type_text = findText(node, "Type");
    if (type_text == nullptr)
    {
        type_text = findText(node, "BaseType");
    }
    if (type_text == nullptr)
    {
        return {CoE::DataType::UNKNOWN, 0, 0};
    }

    CoE::DataType type = resolveType(type_text);
    if (type == CoE::DataType::UNKNOWN)
    {
        return {CoE::DataType::UNKNOWN, 0, 0};
    }

    uint16_t bitlen = requireNumber<uint16_t>(node, "BitSize", "<Type> reference");
    uint16_t bitoff = 0;
    if (char const* bitoff_text = findText(node, "BitOffs"))
    {
        bitoff = parseHexDec<uint16_t>(bitoff_text, "<BitOffs>");
    }

    return {type, bitlen, bitoff};
}

XMLNode* Parser::findNodeType(XMLNode* node, std::string const& where)
{
    char const* raw_type = requireText(node->FirstChildElement("Type"), "Type", where);

    if (dtypes_ == nullptr)
    {
        return nullptr;
    }
    auto dtype = dtypes_->FirstChildElement();
    while (dtype)
    {
        auto name_elem = dtype->FirstChildElement("Name");
        if (name_elem != nullptr)
        {
            char const* name_text = name_elem->GetText();
            if (name_text != nullptr and std::strcmp(raw_type, name_text) == 0)
            {
                break;
            }
        }
        dtype = dtype->NextSiblingElement();
    }

    return dtype;
}

CoE::Object Parser::createObject(XMLNode* node)
{
    CoE::Object object;
    object.index = requireNumber<uint16_t>(node, "Index", "Object");

    std::string where = objectLabel(object.index);
    object.name = requireChildText(node, "Name", where);

    auto [type, bitlen, bitoff] = parseType(node);
    if (CoE::isBasic(type))
    {
        object.code = CoE::ObjectCode::VAR;
        object.entries.resize(1);
        auto& entry = object.entries.at(0);
        entry.subindex = 0;
        entry.bitlen   = bitlen;
        entry.bitoff   = bitoff;
        entry.type     = type;
        entry.access   = loadAccess(node);

        loadDefaultData(node, object, entry);

        return object;
    }

    auto node_type = findNodeType(node, where);
    if (node_type == nullptr)
    {
        if (dtypes_ == nullptr)
        {
            throw std::invalid_argument("ESI: " + where + " references a user-defined <Type> "
                "but no <DataTypes> section is present in <Dictionary>");
        }
        throw std::invalid_argument("ESI: unresolved <Type> reference in " + where);
    }
    auto node_subitem = node_type->FirstChildElement("SubItem");
    while (node_subitem)
    {
        CoE::Entry entry;
        if (char const* text = findText(node_subitem, "Name"))
        {
            entry.description = text;
        }

        auto [subitem_type, subitem_bitlen, subitem_bitoff] = parseType(node_subitem);
        if (CoE::isBasic(subitem_type))
        {
            object.code = CoE::ObjectCode::RECORD;

            entry.type     = subitem_type;
            entry.bitlen   = subitem_bitlen;
            entry.bitoff   = subitem_bitoff;
            entry.subindex = requireNumber<uint8_t>(node_subitem, "SubIdx", where + " SubItem");
            entry.access   = loadAccess(node_subitem);

            object.entries.push_back(std::move(entry));
        }
        else
        {
            object.code = CoE::ObjectCode::ARRAY;

            std::string sub_where = where + " SubItem";
            auto node_array_type = findNodeType(node_subitem, sub_where);
            if (node_array_type == nullptr)
            {
                throw std::invalid_argument("ESI: unresolved <Type> reference in " + sub_where);
            }
            auto [array_type, array_bitlen, array_bitoff] = parseType(node_array_type);
            entry.type   = array_type;
            entry.bitlen = array_bitlen;
            entry.bitoff = array_bitoff;
            entry.access = loadAccess(node_subitem);

            auto node_array_info = node_array_type->FirstChildElement("ArrayInfo");
            if (node_array_info == nullptr)
            {
                throw std::invalid_argument("ESI: missing <ArrayInfo> in " + sub_where);
            }
            uint8_t lbound = requireNumber<uint8_t>(node_array_info, "LBound", sub_where);
            if (lbound == 0)
            {
                entry.subindex = 1;
                entry.bitlen   = requireNumber<uint16_t>(node_array_type, "BitSize", sub_where);
                object.entries.push_back(std::move(entry));
            }
            else
            {
                // uint16_t loop counter is required: with an 8-bit counter and
                // <Elements>255</Elements> (real ETG examples have this), the
                // post-increment wraps 255 -> 0 and the loop never terminates.
                uint16_t elements = requireNumber<uint16_t>(node_array_info, "Elements", sub_where);
                if (elements == 0)
                {
                    throw std::invalid_argument("ESI: <Elements> is zero in " + sub_where);
                }
                if (elements > 0xFF)
                {
                    throw std::invalid_argument("ESI: <Elements> > 255 exceeds CoE SubIndex space in " + sub_where);
                }
                uint16_t total_bitlen   = requireNumber<uint16_t>(node_subitem, "BitSize", sub_where);
                uint16_t element_bitoff = requireNumber<uint16_t>(node_subitem, "BitOffs", sub_where);
                if (total_bitlen % elements != 0)
                {
                    throw std::invalid_argument("ESI: array <BitSize> " + std::to_string(total_bitlen)
                        + " not divisible by <Elements> " + std::to_string(elements) + " in " + sub_where);
                }
                uint16_t element_bitlen = static_cast<uint16_t>(total_bitlen / elements);

                for (uint16_t i = 1; i <= elements; ++i)
                {
                    // Use uint32_t for the intermediate offset arithmetic to
                    // detect the (degenerate but reachable) case where the
                    // final offset exceeds the uint16_t range.
                    uint32_t offset = static_cast<uint32_t>(element_bitoff)
                                    + static_cast<uint32_t>(element_bitlen) * (i - 1);
                    if (offset > 0xFFFF)
                    {
                        throw std::invalid_argument("ESI: array element bitoff " + std::to_string(offset)
                            + " exceeds uint16 range in " + sub_where);
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

        node_subitem = node_subitem->NextSiblingElement("SubItem");
    }

    auto node_info = node->FirstChildElement("Info");
    if (node_info == nullptr)
    {
        return object;
    }
    auto object_subitem = node_info->FirstChildElement("SubItem");
    for (auto& entry : object.entries)
    {
        if (object_subitem == nullptr)
        {
            break;
        }

        if (char const* text = findText(object_subitem, "Name"))
        {
            entry.description = text;
        }
        loadDefaultData(object_subitem, object, entry);

        object_subitem = object_subitem->NextSiblingElement();
    }

    return object;
}

}
