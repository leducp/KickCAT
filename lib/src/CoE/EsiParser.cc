#include <algorithm>
#include <stdexcept>
#include "kickcat/debug.h"

#include "kickcat/CoE/EsiParser.h"

using namespace tinyxml2;

namespace kickcat::CoE
{

    const std::unordered_map<std::string, DataType> EsiParser::BASIC_TYPES
    {
        {"BOOL",  DataType::BOOLEAN    },
        {"BYTE",  DataType::BYTE       },
        {"SINT",  DataType::INTEGER8   },
        {"USINT", DataType::UNSIGNED8  },
        {"INT",   DataType::INTEGER16  },
        {"UINT",  DataType::UNSIGNED16 },
        {"DINT",  DataType::INTEGER32  },
        {"UDINT", DataType::UNSIGNED32 },
        {"LINT",  DataType::INTEGER64  },
        {"ULINT", DataType::UNSIGNED64 },
        {"REAL",  DataType::REAL32     },
    };

    const std::unordered_map<std::string, uint8_t> EsiParser::SM_CONF
    {
        {"MBoxOut",  1},
        {"MBoxIn",   2},
        {"Outputs",  3},
        {"Inputs",   4},
    };


    Dictionary EsiParser::load(std::string const& file)
    {
        XMLError result = doc_.LoadFile(file.c_str());
        if (result != XML_SUCCESS)
        {
            throw std::runtime_error(doc_.ErrorIDToName(result));
        }

        root_ = doc_.RootElement();

        // Helper to find and check a child element, throw if not found
        auto firstChildElement = [](XMLNode* node, char const* name) -> XMLElement*
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
        };

        // Position handler on main entry points
        vendor_ = firstChildElement(root_, "Vendor");
        desc_   = firstChildElement(root_, "Descriptions");

        // jump on profile and associated dictionnary
        devices_    = firstChildElement(desc_,       "Devices");
        device_     = firstChildElement(devices_,    "Device");
        profile_    = firstChildElement(device_,     "Profile");
        dictionary_ = firstChildElement(profile_,    "Dictionary");
        dtypes_     = firstChildElement(dictionary_, "DataTypes");
        objects_    = firstChildElement(dictionary_, "Objects");

        // Load dictionary
        Dictionary dictionary;

        // loop over dictionnary
        auto node_object = objects_->FirstChild();
        while (node_object)
        {
            CoE::Object obj = create(node_object);
            dictionary.push_back(std::move(obj));
            node_object = node_object->NextSibling();
        }

        // load sync managers type object
        CoE::Object sms_type;
        sms_type.index = 0x1c00;
        sms_type.code = ObjectCode::ARRAY;
        sms_type.name = "Sync manager type";

        // create first entry (array size)
        sms_type.entries.push_back(CoE::Entry{0, 8, 0, Access::READ, DataType::UNSIGNED8, "Subindex 0"});

        auto sm = firstChildElement(device_, "Sm");
        while (sm)
        {
            CoE::Entry entry;
            entry.subindex = sms_type.entries.size();
            entry.access = Access::READ;
            entry.bitlen = 8;
            entry.bitoff = sms_type.entries.size() * 8 + 8; // + 8 for padding of the first entry
            entry.description = "Subindex " + std::to_string(sms_type.entries.size());
            entry.type = DataType::UNSIGNED8;
            entry.data = malloc(1);

            uint8_t sm_type = SM_CONF.at(sm->GetText());
            std::memcpy(entry.data, &sm_type, 1);

            sms_type.entries.push_back(std::move(entry));
            sm = sm->NextSiblingElement("Sm");
        }
        auto& subindex0 = sms_type.entries.at(0);
        subindex0.data = malloc(1);
        uint8_t array_size = sms_type.entries.size() - 1;
        std::memcpy(subindex0.data, &array_size, 1);
        dictionary.push_back(std::move(sms_type));

        return dictionary;
    }

    std::vector<uint8_t> EsiParser::loadHexBinary(XMLElement* node)
    {
        std::string field = node->GetText();
        std::vector<uint8_t> data;
        data.reserve(field.size() / 2); // 2 ascii character for one byte

        // Extract hex, data is already LE
        for (std::size_t i = 0; i < field.size(); i += 2)
        {
            std::string hex = field.substr(i, 2);
            uint8_t byte = std::stoi(hex, nullptr, 16);
            data.push_back(byte);
        }

        return data;
    }

    std::vector<uint8_t> EsiParser::loadString(XMLElement* node)
    {
        auto data = loadHexBinary(node);
        std::reverse(data.begin(), data.end());
        return data;
    }

    void EsiParser::loadDefaultData(XMLNode* node, Object& obj, Entry& entry)
    {
        auto node_default_data = node->FirstChildElement("Info")->FirstChildElement("DefaultData");
        if (node_default_data == nullptr)
        {
            return;
        }

        std::vector<uint8_t> data;
        if(entry.type == DataType::VISIBLE_STRING)
        {
            data = loadString(node_default_data);
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
        }
        entry.data = malloc(entry.bitlen / 8);
        std::memcpy(entry.data, data.data(), data.size());
    }

    uint16_t EsiParser::loadAccess(XMLNode* node)
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
            std::string access = node_access->GetText();

            // global read rule
            if (access == "rw" or access == "ro")
            {
                flags |= Access::READ;
            }

            // global write rule
            if (access == "rw" or access == "wo")
            {
                flags |= Access::WRITE;
            }

            auto parseRestrictions = [](char const* raw_restrictions) -> uint16_t
            {
                if (raw_restrictions == nullptr)
                {
                    return Access::READ;
                }

                // lower the string
                std::string restrictions{raw_restrictions};
                std::transform(restrictions.begin(), restrictions.end(), restrictions.begin(),
                    [](char c){ return std::tolower(c); });

                uint16_t result = 0;
                if (restrictions.find("preop")  != std::string::npos) { result |= Access::READ_PREOP;  }
                if (restrictions.find("safeop") != std::string::npos) { result |= Access::READ_SAFEOP; }
                if (restrictions.find("_op")    != std::string::npos) { result |= Access::READ_OP;     }
                if (restrictions.find("op") == 0)                     { result |= Access::READ_OP;     }

                return result;
            };

            // restrictions
            uint16_t restrictions_mask = 0;
            restrictions_mask |= (parseRestrictions(node_access->Attribute("ReadRestrictions"))  << 0);
            restrictions_mask |= (parseRestrictions(node_access->Attribute("WriteRestrictions")) << 3);

            flags &= restrictions_mask;
        }
        else
        {
            flags |= Access::READ; // Default value
        }

        auto node_pdo_mapping = node_flags->FirstChildElement("PdoMapping");
        if (node_pdo_mapping != nullptr)
        {
            std::string mapping = node_pdo_mapping->GetText();
            for (auto const& c : mapping)
            {
                if (std::tolower(c) == 'r') { flags |= Access::RxPDO; }
                if (std::tolower(c) == 't') { flags |= Access::TxPDO; }
            }
        }

        auto node_backup = node_flags->FirstChildElement("Backup");
        if (node_backup != nullptr)
        {
            if (node_backup->GetText()[0] == '1') { flags |= Access::BACKUP; }
        }

        auto node_setting = node_flags->FirstChildElement("Setting");
        if (node_setting != nullptr)
        {
            if (node_setting->GetText()[0] == '1') { flags |= Access::SETTING; }
        }

        return flags;
    }

    std::tuple<DataType, uint16_t, uint16_t> EsiParser::parseType(XMLNode* node)
    {
        auto node_type = node->FirstChildElement("Type");
        if (not node_type)
        {
            node_type = node->FirstChildElement("BaseType");
        }



        auto it = BASIC_TYPES.find(node_type->GetText());
        if (it != BASIC_TYPES.end())
        {
            uint16_t bitlen = toNumber<uint16_t>(node->FirstChildElement("BitSize"));
            uint16_t bitoff = 0;
            auto node_bitoff = node->FirstChildElement("BitOffs");
            if (node_bitoff)
            {
                bitoff = toNumber<uint16_t>(node_bitoff);
            }

            return {it->second, bitlen, bitoff};
        }

        if(strstr(node_type->GetText(), "STRING"))
        {
            uint32_t bitlen = toNumber<uint32_t>(node->FirstChildElement("BitSize"));
            return {DataType::VISIBLE_STRING, bitlen, 0};
        }

        return {DataType::UNKNOWN, 0, 0};
    }


    XMLNode* EsiParser::findNodeType(XMLNode* node)
    {
        std::string raw_type = node->FirstChildElement("Type")->GetText();

        auto dtype = dtypes_->FirstChild();
        while (dtype)
        {
            if (raw_type == dtype->FirstChildElement("Name")->GetText())
            {
                break;
            }
            dtype = dtype->NextSibling();
        }

        return dtype;
    }


    Object EsiParser::create(XMLNode* node)
    {
        Object object;
        object.index = toNumber<uint16_t>(node->FirstChildElement("Index"));
        object.name  = node->FirstChildElement("Name")->GetText();
        auto [type, bitlen, bitoff] = parseType(node);
        if (isBasic(type))
        {
            // Basic type: no subindex in the ESI file because it is defined directly in the object node.
            object.code = ObjectCode::VAR;
            object.entries.resize(1);
            auto& entry = object.entries.at(0);
            entry.subindex = 0;
            entry.bitlen = bitlen;
            entry.bitoff = bitoff;
            entry.type = type;
            entry.access = loadAccess(node);

            loadDefaultData(node, object, entry);

            return object;
        }

        auto node_type = findNodeType(node);
        auto node_subitem = node_type->FirstChildElement("SubItem");
        while (node_subitem)
        {
            Entry entry;
            auto node_name = node_subitem->FirstChildElement("Name");
            if (node_name)
            {
                entry.description = node_name->GetText();
            }

            auto [subitem_type, subitem_bitlen, subitem_bitoff] = parseType(node_subitem);
            if (isBasic(subitem_type))
            {
                object.code  = ObjectCode::RECORD;

                entry.type   = subitem_type;
                entry.bitlen = subitem_bitlen;
                entry.bitoff = subitem_bitoff;
                entry.subindex = toNumber<uint8_t>(node_subitem->FirstChildElement("SubIdx"));
                entry.access = loadAccess(node_subitem);

                object.entries.push_back(std::move(entry));
            }
            else
            {
                object.code = ObjectCode::ARRAY;

                auto node_array_type = findNodeType(node_subitem);
                auto [array_type, array_bitlen, array_bitoff] = parseType(node_array_type);
                entry.type   = array_type;
                entry.bitlen = array_bitlen;
                entry.bitoff = array_bitoff;
                entry.access = loadAccess(node_subitem);

                auto node_array_info = node_array_type->FirstChildElement("ArrayInfo");
                uint8_t lbound = toNumber<uint8_t>(node_array_info->FirstChildElement("LBound"));
                if (lbound == 0)
                {
                    // one big entry which is an array:
                    // - bitlen shall be updated accordingly
                    // - elements cannot be used because it represents the internal elements, not the elements accessible
                    //   through subindex
                    entry.subindex = 1;
                    entry.bitlen = toNumber<uint16_t>(node_array_type->FirstChildElement("BitSize"));
                    object.entries.push_back(std::move(entry));
                }
                else
                {
                    // array entries are the subindex starting from 1, 0 is the array size
                    uint8_t elements = toNumber<uint8_t>(node_array_info->FirstChildElement("Elements"));
                    uint16_t element_bitlen = toNumber<uint16_t>(node_subitem->FirstChildElement("BitSize")) / elements;
                    uint16_t element_bitoff = toNumber<uint16_t>(node_subitem->FirstChildElement("BitOffs"));

                    for (uint8_t i = 1; i <= elements; ++i)
                    {
                        entry.bitlen = element_bitlen;
                        entry.bitoff = element_bitoff + element_bitlen * (i - 1);
                        entry.subindex = i;
                        object.entries.push_back(std::move(entry));
                    }
                }
            }

            node_subitem = node_subitem->NextSiblingElement("SubItem");
        }


        // Set default data value
        // Update name if possible by using the object node
        auto object_subitem = node->FirstChildElement("Info")->FirstChildElement("SubItem");
        auto entry = object.entries.begin();
        for (auto& object_entry : object.entries)
        {
            if (object_subitem)
            {
                auto object_subitem_name = object_subitem->FirstChildElement("Name");
                if (object_subitem_name)
                {
                    object_entry.description = object_subitem_name->GetText();
                }

                loadDefaultData(object_subitem, object, *entry);

                entry++;
                object_subitem = object_subitem->NextSiblingElement();
            }
        }

        return object;
    }
}
