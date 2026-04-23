#include "MasterOD.h"
#include "kickcat/Error.h"
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
        // Guard against a second call invalidating previously-captured ConfigurationData pointers.
        for (auto const& obj : dict)
        {
            if ((obj.index & 0xFF00) == 0x8000)
            {
                THROW_ERROR("MasterOD::populate called twice on the same dictionary");
            }
        }

        configuration_data_.clear();

        for (uint16_t i = 0; i < slaves.size(); ++i)
        {
            auto const& slave = slaves[i];
            auto const& sii = slave.sii;
            uint16_t index = 0x8000 + i;

            std::string slave_name  = slave.name();
            std::string slave_type  = slave.type();
            uint16_t    name_bitlen = static_cast<uint16_t>(slave_name.size() * 8);
            uint16_t    type_bitlen = static_cast<uint16_t>(slave_type.size() * 8);

            // ETG.1510 :38 is a UNSIGNED16 reassembled from SII general's four 4-bit port fields.
            uint16_t const port_physics = static_cast<uint16_t>(
                  (sii.general.port_0 & 0xF)
                | ((sii.general.port_1 & 0xF) << 4)
                | ((sii.general.port_2 & 0xF) << 8)
                | ((sii.general.port_3 & 0xF) << 12));

            // Zero defaults for entries whose source is not yet plumbed (ENI, slave 0x1000 SDO,
            // slave OD probe for 0x10F3). The dictionary shape stays stable across future PRs.
            uint32_t const device_type      = 0;
            uint8_t  const link_status      = 0;
            uint8_t  const link_preset      = 0;
            uint8_t  const flags            = 0;
            uint8_t  const diag_history_obj = 0;

            CoE::Object obj{index, CoE::ObjectCode::RECORD, "Slave Configuration", {}};

            // Sub-0 = largest supported subindex (CiA-301 convention for RECORD).
            uint16_t bit_offset = 0;
            CoE::addEntry<uint8_t>    (obj, 0,  8,           bit_offset, CoE::Access::READ, CoE::DataType::UNSIGNED8,      "Number of Entries",           uint8_t{40});
            bit_offset += 8;
            CoE::addEntry<uint16_t>   (obj, 1,  16,          bit_offset, CoE::Access::READ, CoE::DataType::UNSIGNED16,     "Fixed Station Address",       slave.address);
            bit_offset += 16;
            CoE::addEntry<char const*>(obj, 2,  type_bitlen, bit_offset, CoE::Access::READ, CoE::DataType::VISIBLE_STRING, "Type",                        slave_type.c_str());
            bit_offset += type_bitlen;
            CoE::addEntry<char const*>(obj, 3,  name_bitlen, bit_offset, CoE::Access::READ, CoE::DataType::VISIBLE_STRING, "Name",                        slave_name.c_str());
            bit_offset += name_bitlen;
            CoE::addEntry<uint32_t>   (obj, 4,  32,          bit_offset, CoE::Access::READ, CoE::DataType::UNSIGNED32,     "Device Type",                 device_type);
            bit_offset += 32;
            CoE::addEntry<uint32_t>   (obj, 5,  32,          bit_offset, CoE::Access::READ, CoE::DataType::UNSIGNED32,     "Vendor Id",                   sii.info.vendor_id);
            bit_offset += 32;
            CoE::addEntry<uint32_t>   (obj, 6,  32,          bit_offset, CoE::Access::READ, CoE::DataType::UNSIGNED32,     "Product Code",                sii.info.product_code);
            bit_offset += 32;
            CoE::addEntry<uint32_t>   (obj, 7,  32,          bit_offset, CoE::Access::READ, CoE::DataType::UNSIGNED32,     "Revision Number",             sii.info.revision_number);
            bit_offset += 32;
            CoE::addEntry<uint32_t>   (obj, 8,  32,          bit_offset, CoE::Access::READ, CoE::DataType::UNSIGNED32,     "Serial Number",               sii.info.serial_number);
            bit_offset += 32;
            CoE::addEntry<uint16_t>   (obj, 33, 16,          bit_offset, CoE::Access::READ, CoE::DataType::UNSIGNED16,     "Mailbox Out Size",            sii.info.standard_recv_mbx_size);
            bit_offset += 16;
            CoE::addEntry<uint16_t>   (obj, 34, 16,          bit_offset, CoE::Access::READ, CoE::DataType::UNSIGNED16,     "Mailbox In Size",             sii.info.standard_send_mbx_size);
            bit_offset += 16;
            CoE::addEntry<uint8_t>    (obj, 35, 8,           bit_offset, CoE::Access::READ, CoE::DataType::UNSIGNED8,      "Link Status",                 link_status);
            bit_offset += 8;
            CoE::addEntry<uint8_t>    (obj, 36, 8,           bit_offset, CoE::Access::READ, CoE::DataType::UNSIGNED8,      "Link Preset",                 link_preset);
            bit_offset += 8;
            CoE::addEntry<uint8_t>    (obj, 37, 8,           bit_offset, CoE::Access::READ, CoE::DataType::UNSIGNED8,      "Flags",                       flags);
            bit_offset += 8;
            CoE::addEntry<uint16_t>   (obj, 38, 16,          bit_offset, CoE::Access::READ, CoE::DataType::UNSIGNED16,     "Port Physics",                port_physics);
            bit_offset += 16;
            CoE::addEntry<uint16_t>   (obj, 39, 16,          bit_offset, CoE::Access::READ, CoE::DataType::UNSIGNED16,     "Mailbox Protocols Supported", sii.info.mailbox_protocol);
            bit_offset += 16;
            CoE::addEntry<uint8_t>    (obj, 40, 8,           bit_offset, CoE::Access::READ, CoE::DataType::BOOLEAN,        "Diag History Object Supported", diag_history_obj);

            // Entries-vector buffer survives the move into dict, so these pointers stay valid.
            ConfigurationData config;
            config.fixed_address    = &obj.entries[1];
            config.vendor_id        = &obj.entries[5];
            config.product_code     = &obj.entries[6];
            config.revision         = &obj.entries[7];
            config.serial_number    = &obj.entries[8];
            config.mailbox_out_size = &obj.entries[9];
            config.mailbox_in_size  = &obj.entries[10];

            dict.push_back(std::move(obj));
            configuration_data_.push_back(config);
        }
    }
}
