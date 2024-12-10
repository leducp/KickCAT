#ifndef KICKCAT_COE_ESI_PARSER_H
#define KICKCAT_COE_ESI_PARSER_H

#include <tinyxml2.h>
#include <unordered_map>

#include "kickcat/CoE/OD.h"

namespace kickcat::CoE
{
    class EsiParser
    {
    public:
        EsiParser() = default;
        ~EsiParser() = default;

        CoE::Dictionary load(std::string const& file);

        char const* vendor() const  { return vendor_->FirstChildElement("Name")->GetText();       }
        char const* profile() const { return profile_->FirstChildElement("ProfileNo")->GetText(); }

    private:
        template<typename T>
        T toNumber(tinyxml2::XMLElement* node)
        {
            std::string field = node->GetText();
            if (field.rfind("#x", 0) == 0)
            {
                field[0] = '0';
            }
            return std::stoi(field, nullptr, 0);
        }

        std::vector<uint8_t> loadHexBinary(tinyxml2::XMLElement* node);
        std::vector<uint8_t> loadString(tinyxml2::XMLElement* node);

        void loadDefaultData(tinyxml2::XMLNode* node, Object& obj, Entry& entry);

        uint16_t loadAccess(tinyxml2::XMLNode* node);

        std::tuple<DataType, uint16_t, uint16_t> parseType(tinyxml2::XMLNode* node);

        tinyxml2::XMLNode* findNodeType(tinyxml2::XMLNode* node);

        Object create(tinyxml2::XMLNode* node);


        // Manage XML entry point
        tinyxml2::XMLDocument doc_;
        tinyxml2::XMLElement* root_;

        // second level
        tinyxml2::XMLElement* vendor_;
        tinyxml2::XMLElement* desc_;

        // jump on profile and associated dictionnary
        tinyxml2::XMLElement* profile_;
        tinyxml2::XMLElement* devices_;
        tinyxml2::XMLElement* device_;
        tinyxml2::XMLElement* dictionary_;
        tinyxml2::XMLElement* dtypes_;
        tinyxml2::XMLElement* objects_;

        static const std::unordered_map<std::string, DataType> BASIC_TYPES;
        static const std::unordered_map<std::string, uint8_t> SM_CONF;
    };
}

#endif
