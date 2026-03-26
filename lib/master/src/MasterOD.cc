#include "MasterOD.h"
#include "kickcat/Slave.h"

namespace kickcat
{
    MasterOD::MasterOD(MasterIdentity const& identity)
        : identity_(identity)
    {
    }


    CoE::Dictionary MasterOD::createDictionary()
    {
        CoE::Dictionary dict;

        // 0x1000: Device Type
        {
            CoE::Object obj{0x1000, CoE::ObjectCode::VAR, "Device Type", {}};
            CoE::addEntry<uint32_t>(obj, 0, 32, 0, CoE::Access::READ, CoE::DataType::UNSIGNED32, "Device Type", identity_.device_type);
            dict.push_back(std::move(obj));
        }

        // 0x1008: Manufacturer Device Name
        if (not identity_.device_name.empty())
        {
            CoE::Object obj{0x1008, CoE::ObjectCode::VAR, "Manufacturer Device Name", {}};
            uint16_t bitlen = static_cast<uint16_t>(identity_.device_name.size() * 8);
            CoE::addEntry<char const*>(obj, 0, bitlen, 0, CoE::Access::READ, CoE::DataType::VISIBLE_STRING, "Device Name", identity_.device_name.c_str());
            dict.push_back(std::move(obj));
        }

        // 0x1009: Manufacturer Hardware Version
        if (not identity_.hardware_version.empty())
        {
            CoE::Object obj{0x1009, CoE::ObjectCode::VAR, "Manufacturer Hardware Version", {}};
            uint16_t bitlen = static_cast<uint16_t>(identity_.hardware_version.size() * 8);
            CoE::addEntry<char const*>(obj, 0, bitlen, 0, CoE::Access::READ, CoE::DataType::VISIBLE_STRING, "Hardware Version", identity_.hardware_version.c_str());
            dict.push_back(std::move(obj));
        }

        // 0x100A: Manufacturer Software Version
        if (not identity_.software_version.empty())
        {
            CoE::Object obj{0x100A, CoE::ObjectCode::VAR, "Manufacturer Software Version", {}};
            uint16_t bitlen = static_cast<uint16_t>(identity_.software_version.size() * 8);
            CoE::addEntry<char const*>(obj, 0, bitlen, 0, CoE::Access::READ, CoE::DataType::VISIBLE_STRING, "Software Version", identity_.software_version.c_str());
            dict.push_back(std::move(obj));
        }

        // 0x1018: Identity Object
        {
            CoE::Object obj{0x1018, CoE::ObjectCode::RECORD, "Identity Object", {}};
            CoE::addEntry<uint8_t> (obj, 0, 8,  0,   CoE::Access::READ, CoE::DataType::UNSIGNED8,  "Number of Entries", 4);
            CoE::addEntry<uint32_t>(obj, 1, 32, 16,  CoE::Access::READ, CoE::DataType::UNSIGNED32, "Vendor ID",         identity_.vendor_id);
            CoE::addEntry<uint32_t>(obj, 2, 32, 48,  CoE::Access::READ, CoE::DataType::UNSIGNED32, "Product Code",      identity_.product_code);
            CoE::addEntry<uint32_t>(obj, 3, 32, 80,  CoE::Access::READ, CoE::DataType::UNSIGNED32, "Revision Number",   identity_.revision);
            CoE::addEntry<uint32_t>(obj, 4, 32, 112, CoE::Access::READ, CoE::DataType::UNSIGNED32, "Serial Number",     identity_.serial_number);
            dict.push_back(std::move(obj));
        }

        return dict;
    }


    void MasterOD::populate(CoE::Dictionary& dict, std::vector<Slave> const& slaves)
    {
        configuration_data_.clear();

        for (uint16_t i = 0; i < slaves.size(); ++i)
        {
            auto const& slave = slaves[i];
            auto const& sii = slave.sii;
            uint16_t index = 0x8000 + i;

            CoE::Object obj{index, CoE::ObjectCode::RECORD, "Slave Configuration", {}};
            CoE::addEntry<uint8_t> (obj, 0,  8,  0,   CoE::Access::READ, CoE::DataType::UNSIGNED8,  "Number of Entries", 7);
            CoE::addEntry<uint16_t>(obj, 1,  16, 8,   CoE::Access::READ, CoE::DataType::UNSIGNED16, "Fixed Station Address", slave.address);
            CoE::addEntry<uint32_t>(obj, 5,  32, 24,  CoE::Access::READ, CoE::DataType::UNSIGNED32, "Vendor Id",         sii.vendor_id);
            CoE::addEntry<uint32_t>(obj, 6,  32, 56,  CoE::Access::READ, CoE::DataType::UNSIGNED32, "Product Code",      sii.product_code);
            CoE::addEntry<uint32_t>(obj, 7,  32, 88,  CoE::Access::READ, CoE::DataType::UNSIGNED32, "Revision Number",   sii.revision_number);
            CoE::addEntry<uint32_t>(obj, 8,  32, 120, CoE::Access::READ, CoE::DataType::UNSIGNED32, "Serial Number",     sii.serial_number);
            CoE::addEntry<uint16_t>(obj, 33, 16, 152, CoE::Access::READ, CoE::DataType::UNSIGNED16, "Mailbox Out Size",  sii.mailbox_recv_size);
            CoE::addEntry<uint16_t>(obj, 34, 16, 168, CoE::Access::READ, CoE::DataType::UNSIGNED16, "Mailbox In Size",   sii.mailbox_send_size);

            dict.push_back(std::move(obj));

            auto& entries = dict.back().entries;
            ConfigurationData config;
            config.fixed_address    = &entries[1];
            config.vendor_id        = &entries[2];
            config.product_code     = &entries[3];
            config.revision         = &entries[4];
            config.serial_number    = &entries[5];
            config.mailbox_out_size = &entries[6];
            config.mailbox_in_size  = &entries[7];

            configuration_data_.push_back(config);
        }
    }
}
