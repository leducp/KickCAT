#ifndef KICKCAT_ESI_PARSER_H
#define KICKCAT_ESI_PARSER_H

#include <tinyxml2.h>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "kickcat/CoE/OD.h"
#include "kickcat/ESI/Device.h"

namespace kickcat::ESI
{
    class Parser
    {
    public:
        Parser() = default;
        ~Parser() = default;

        CoE::Dictionary loadFile  (std::string const& file);
        CoE::Dictionary loadString(std::string const& xml);

        char const* vendor()  const { return vendor_name_.c_str(); }
        char const* profile() const { return profile_no_.c_str();  }

        std::vector<DeviceSummary> listDevices      (std::string const& file);
        std::vector<DeviceSummary> listDevicesString(std::string const& xml);

        Device loadDevice      (std::string const& file, DeviceFilter const& filter = {});
        Device loadDeviceString(std::string const& xml,  DeviceFilter const& filter = {});

    private:
        template<typename T>
        T toNumber(tinyxml2::XMLElement* node)
        {
            std::string field = node->GetText();
            if (field.rfind("#x", 0) == 0)
            {
                field[0] = '0';
            }
            return static_cast<T>(std::stoll(field, nullptr, 0));
        }

        static std::optional<uint32_t> readHexDecAttr(tinyxml2::XMLElement* node, char const* name);

        void openFile  (std::string const& file);
        void openString(std::string const& xml);
        void resolveTopLevel();

        std::vector<DeviceSummary> listDevicesImpl();
        Device                     loadDeviceImpl(DeviceFilter const& filter);

        tinyxml2::XMLElement* selectDevice(DeviceFilter const& filter);
        DeviceSummary         summarize   (tinyxml2::XMLElement* device);

        CoE::Dictionary buildDictionary(tinyxml2::XMLElement* device, tinyxml2::XMLElement* profile);

        std::vector<uint8_t> loadHexBinary(tinyxml2::XMLElement* node);
        std::vector<uint8_t> loadStringData(tinyxml2::XMLElement* node);

        void loadDefaultData(tinyxml2::XMLNode* node, CoE::Object& obj, CoE::Entry& entry);
        uint16_t loadAccess(tinyxml2::XMLNode* node);

        std::tuple<CoE::DataType, uint16_t, uint16_t> parseType(tinyxml2::XMLNode* node);
        CoE::DataType         resolveType (std::string const& type_name);
        tinyxml2::XMLNode*    findNodeType(tinyxml2::XMLNode* node);

        CoE::Object createObject(tinyxml2::XMLNode* node);

        tinyxml2::XMLDocument doc_;
        tinyxml2::XMLElement* root_       = nullptr;
        tinyxml2::XMLElement* vendor_xml_ = nullptr;
        tinyxml2::XMLElement* devices_    = nullptr;

        tinyxml2::XMLElement* dtypes_ = nullptr;

        std::string vendor_name_;
        std::string profile_no_;

        static const std::unordered_map<std::string, CoE::DataType> BASIC_TYPES;
        static const std::unordered_map<std::string, uint8_t>       SM_CONF;
    };
}

#endif
