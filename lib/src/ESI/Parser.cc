#include <algorithm>
#include <cstring>
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

const std::unordered_map<std::string, uint8_t> Parser::SM_CONF
{
    {"MBoxOut",  1},
    {"MBoxIn",   2},
    {"Outputs",  3},
    {"Inputs",   4},
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

    // Parent + child-name overload: fetches then validates.
    char const* requireText(XMLNode* parent, char const* child, std::string const& where)
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

    int64_t parseHexDec(std::string text)
    {
        if (text.empty())
        {
            throw std::invalid_argument("ESI: empty numeric value");
        }
        if (text.rfind("#x", 0) == 0)
        {
            if (text.size() == 2)
            {
                throw std::invalid_argument("ESI: '#x' with no hex digits");
            }
            text[0] = '0';
            return std::stoll(text, nullptr, 16);
        }
        if (text.rfind("0x", 0) == 0 or text.rfind("0X", 0) == 0)
        {
            if (text.size() == 2)
            {
                throw std::invalid_argument("ESI: '0x' with no hex digits");
            }
            return std::stoll(text, nullptr, 16);
        }
        return std::stoll(text, nullptr, 10);
    }

    template<typename T>
    T requireNumber(XMLNode* parent, char const* child, std::string const& where)
    {
        char const* text = requireText(parent, child, where);
        return static_cast<T>(parseHexDec(text));
    }

    std::string objectLabel(uint16_t index)
    {
        char buf[24];
        std::snprintf(buf, sizeof(buf), "Object 0x%04x", index);
        return buf;
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
    return static_cast<uint32_t>(parseHexDec(raw));
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
    root_       = doc_.RootElement();
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
    auto* device_node  = selectDevice(filter);
    auto* profile_node = requireChild(device_node, "Profile");

    Device device;
    device.vendor_name = vendor_name_;
    if (auto* id = vendor_xml_->FirstChildElement("Id"); id != nullptr)
    {
        char const* text = id->GetText();
        if (text != nullptr and *text != '\0')
        {
            device.vendor_id = static_cast<uint32_t>(parseHexDec(text));
        }
    }

    auto* type_node = device_node->FirstChildElement("Type");
    device.type         = textOrEmpty(type_node);
    device.product_code = readHexDecAttr(type_node, "ProductCode").value_or(0);
    device.revision_no  = readHexDecAttr(type_node, "RevisionNo" ).value_or(0);
    device.serial_no    = readHexDecAttr(type_node, "SerialNo"   ).value_or(0);
    device.name         = textOrEmpty(device_node->FirstChildElement("Name"));
    device.group_type   = textOrEmpty(device_node->FirstChildElement("GroupType"));

    auto* profile_no = profile_node->FirstChildElement("ProfileNo");
    profile_no_ = textOrEmpty(profile_no);
    if (not profile_no_.empty())
    {
        device.profile_no = static_cast<uint16_t>(parseHexDec(profile_no_));
    }

    device.dictionary = buildDictionary(device_node, profile_node);
    return device;
}

CoE::Dictionary Parser::buildDictionary(XMLElement* device, XMLElement* profile)
{
    auto* dictionary = requireChild(profile,    "Dictionary");
    dtypes_          = requireChild(dictionary, "DataTypes");
    auto* objects    = requireChild(dictionary, "Objects");

    CoE::Dictionary out;

    for (auto* node_object = objects->FirstChildElement(); node_object != nullptr; node_object = node_object->NextSiblingElement())
    {
        out.push_back(createObject(node_object));
    }

    CoE::Object sms_type;
    sms_type.index = 0x1c00;
    sms_type.code  = CoE::ObjectCode::ARRAY;
    sms_type.name  = "Sync manager type";
    sms_type.entries.push_back(CoE::Entry{0, 8, 0, CoE::Access::READ, CoE::DataType::UNSIGNED8, "Subindex 0"});

    for (auto* sm = device->FirstChildElement("Sm"); sm != nullptr; sm = sm->NextSiblingElement("Sm"))
    {
        CoE::Entry entry;
        entry.subindex    = static_cast<uint8_t>(sms_type.entries.size());
        entry.access      = CoE::Access::READ;
        entry.bitlen      = 8;
        entry.bitoff      = static_cast<uint16_t>(sms_type.entries.size() * 8 + 8);
        entry.description = "Subindex " + std::to_string(sms_type.entries.size());
        entry.type        = CoE::DataType::UNSIGNED8;
        entry.data        = std::malloc(1);

        char const* sm_text = textOrEmpty(sm);
        auto it = SM_CONF.find(sm_text);
        uint8_t sm_type = 0;
        if (it != SM_CONF.end())
        {
            sm_type = it->second;
        }
        std::memcpy(entry.data, &sm_type, 1);

        sms_type.entries.push_back(std::move(entry));
    }
    auto& subindex0 = sms_type.entries.at(0);
    subindex0.data = std::malloc(1);
    uint8_t array_size = static_cast<uint8_t>(sms_type.entries.size() - 1);
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
    std::vector<uint8_t> data;
    data.reserve(field.size() / 2);

    for (std::size_t i = 0; i < field.size(); i += 2)
    {
        std::string hex = field.substr(i, 2);
        uint8_t byte = static_cast<uint8_t>(std::stoi(hex, nullptr, 16));
        data.push_back(byte);
    }

    return data;
}

std::vector<uint8_t> Parser::loadStringData(XMLElement* node)
{
    auto data = loadHexBinary(node);
    std::reverse(data.begin(), data.end());
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
        std::vector<uint8_t> data;
        if (entry.type == CoE::DataType::VISIBLE_STRING)
        {
            data = loadStringData(node_default_data);
        }
        else
        {
            data = loadHexBinary(node_default_data);
        }

        if (data.size() != (entry.bitlen / 8))
        {
            esi_warning("Cannot load default data for 0x%04x.%d, expected size mismatch.\n"
                    "-> Got %ld bits, expected: %d bit\n"
                    "==> Skipping entry\n",
                obj.index, entry.subindex,
                data.size() * 8, entry.bitlen);
            return;
        }
        entry.data = std::malloc(entry.bitlen / 8);
        std::memcpy(entry.data, data.data(), data.size());
        return;
    }

    if (char const* default_value = findText(node_info, "DefaultValue"))
    {
        int64_t value = parseHexDec(default_value);
        uint32_t size = entry.bitlen / 8;
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
        bitoff = static_cast<uint16_t>(parseHexDec(bitoff_text));
    }

    return {type, bitlen, bitoff};
}

XMLNode* Parser::findNodeType(XMLNode* node, std::string const& where)
{
    char const* raw_type = requireText(node->FirstChildElement("Type"), "Type", where);

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
    object.name = requireText(node, "Name", where);

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
                uint8_t  elements       = requireNumber<uint8_t> (node_array_info, "Elements", sub_where);
                if (elements == 0)
                {
                    throw std::invalid_argument("ESI: <Elements> is zero in " + sub_where);
                }
                uint16_t element_bitlen = static_cast<uint16_t>(requireNumber<uint16_t>(node_subitem, "BitSize", sub_where) / elements);
                uint16_t element_bitoff = requireNumber<uint16_t>(node_subitem, "BitOffs", sub_where);

                for (uint8_t i = 1; i <= elements; ++i)
                {
                    entry.bitlen   = element_bitlen;
                    entry.bitoff   = static_cast<uint16_t>(element_bitoff + element_bitlen * (i - 1));
                    entry.subindex = i;
                    object.entries.push_back(std::move(entry));
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
