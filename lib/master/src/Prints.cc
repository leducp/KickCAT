#include "Prints.h"

#include <algorithm>
#include <functional>
#include <iomanip>

namespace kickcat
{
    void printInfo(Slave const& slave)
    {
        std::stringstream os;
        os << "\n -*-*-*-*- slave " << std::to_string(slave.address) << " -*-*-*-*-\n";

        // SII identity strings (Category General, ETG.2010): what the device calls itself.
        // Absent categories resolve to an empty string index, hence the placeholders.
        auto orEmpty = [](std::string_view s) -> std::string
        { return s.empty() ? std::string{"<none>"} : std::string{s}; };

        os << "Name:            " << slave.name() << "\n";
        os << "Type:            " << orEmpty(slave.type()) << "\n";
        os << "Group:           " << orEmpty(slave.sii.getString(slave.sii.general.group_info_id))  << "\n";
        os << "Image:           " << orEmpty(slave.sii.getString(slave.sii.general.image_name_id))  << "\n";
        os << "Station alias:   " << "0x" << std::setfill('0') << std::setw(4) << std::hex << slave.sii.info.station_alias << "\n";
        os << "Vendor ID:       " << "0x" << std::setfill('0') << std::setw(8) << std::hex << slave.sii.info.vendor_id << "\n";
        os << "Product code:    " << "0x" << std::setfill('0') << std::setw(8) << std::hex << slave.sii.info.product_code << "\n";
        os << "Revision number: " << "0x" << std::setfill('0') << std::setw(8) << std::hex << slave.sii.info.revision_number << "\n";
        os << "Serial number:   " << "0x" << std::setfill('0') << std::setw(8) << std::hex << slave.sii.info.serial_number << "\n";
        os << "mailbox in:  size " << std::dec << slave.mailbox.recv_size << " - offset " << "0x" << std::setfill('0')
            << std::setw(4) << std::hex << slave.mailbox.recv_offset << "\n";

        os << "mailbox out: size " << std::dec << slave.mailbox.send_size << " - offset " << "0x" << std::setfill('0')
            << std::setw(4) << std::hex << slave.mailbox.send_offset << "\n";

        os << "supported mailbox protocols: ";
        std::string supported_protocols;
        if (slave.sii.info.mailbox_protocol & eeprom::MailboxProtocol::AoE)
        {
            supported_protocols += "AoE, ";
        }
        if (slave.sii.info.mailbox_protocol & eeprom::MailboxProtocol::CoE)
        {
            supported_protocols += "CoE, ";
        }
        if (slave.sii.info.mailbox_protocol & eeprom::MailboxProtocol::EoE)
        {
            supported_protocols += "EoE, ";
        }
        if (slave.sii.info.mailbox_protocol & eeprom::MailboxProtocol::FoE)
        {
            supported_protocols += "FoE, ";
        }
        if (slave.sii.info.mailbox_protocol & eeprom::MailboxProtocol::SoE)
        {
            supported_protocols += "SoE, ";
        }
        if (supported_protocols.empty())
        {
            supported_protocols = "None";
        }
        else
        {
            // remove last ", "
            supported_protocols.pop_back();
            supported_protocols.pop_back();
        }
        os << supported_protocols << "\n";

        os << "EEPROM: size: " << std::dec << slave.sii.eepromSizeBytes() << " - version "<< "0x" << std::setfill('0')
            << std::setw(2) << std::hex << slave.sii.info.version << "\n";

        os << "\nSII size: " << std::dec << slave.sii.eepromSizeBytes() << "\n";

        for (size_t i = 0; i < slave.sii.fmmus.size(); ++i)
        {
            os << "FMMU[" << std::to_string(i) << "] " << fmmuTypeToString(slave.sii.fmmus[i]) << "\n";
        }

        for (size_t i = 0; i < slave.sii.syncManagers.size(); ++i)
        {
            auto const& sm = slave.sii.syncManagers[i];
            os << "SM[" << std::dec << i << "] config\n";
            os << "     physical address: " << "0x" << std::hex << sm.start_address << "\n";
            os << "     length:           " << std::dec << sm.length << "\n";
            os << "     type:             " << std::dec << toString(static_cast<SyncManager::Type>(sm.type)) << "\n";
            os << "     control:          " << std::hex << (int)sm.control_register << "\n";
        }

        printf("%s", os.str().c_str());
    }

    void printPDOs(Slave const& slave)
    {
        std::stringstream os;

        // The mapping object index (0x16xx for RxPDO, 0x1Axx for TxPDO) and the SyncManager it is
        // assigned to are what a master needs to re-map the process data: print them per mapping
        // instead of flattening every entry into one list.
        auto printMapping = [&](char const* direction, eeprom::PDOMapping const& mapping)
        {
            os << direction << " 0x" << std::setfill('0') << std::setw(4) << std::hex << mapping.index
               << " - SM" << std::dec << static_cast<uint16_t>(mapping.sync_manager);

            auto name = slave.sii.getString(mapping.name_index);
            if (not name.empty())
            {
                os << " - " << name;
            }
            os << "\n";

            int32_t bits = 0;
            for (auto const& entry : mapping.entries)
            {
                os << "    (0x" << std::setfill('0') << std::setw(4) << std::hex << entry.index <<
                    " ; 0x" << std::setfill('0') << std::setw(2) << std::hex << static_cast<uint16_t>(entry.subindex) <<
                    ") - " << std::dec << std::to_string(entry.bitlen) << " bit(s) - "
                    << slave.sii.getString(entry.name) << "\n";
                bits += entry.bitlen;
            }
            os << "    total: " << std::dec << bits << " bit(s)\n";
        };

        for (auto const& mapping : slave.sii.RxPDO)
        {
            printMapping("RxPDO", mapping);
        }

        for (auto const& mapping : slave.sii.TxPDO)
        {
            printMapping("TxPDO", mapping);
        }

        printf("%s", os.str().c_str());
    }


    void printESC(Slave const& slave)
    {
        printf( "\n **** ESC Description ****\n" );
        printf("Type:          %s (0x%x)\n", typeToString(slave.esc.type), slave.esc.type);
        printf("Revision:      0x%02x\n", slave.esc.revision);
        printf("Build:         0x%04x\n", slave.esc.build);
        printf("FMMUs:         %d\n", slave.esc.fmmus);
        printf("SyncManagers:  %d\n", slave.esc.syncManagers);
        printf("RAM Size:      %d KB\n", slave.esc.ram_size);
        printf("Port 0:        %s\n", portToString(slave.esc.ports >> 0));
        printf("Port 1:        %s\n", portToString(slave.esc.ports >> 2));
        printf("Port 2:        %s\n", portToString(slave.esc.ports >> 4));
        printf("Port 3:        %s\n", portToString(slave.esc.ports >> 6));
        printf("Features:      \n%s\n", featuresToString(slave.esc.features).c_str());
    }


    namespace
    {
        // Walk the parent map as a tree, delegating the node text to `label` so callers can
        // decorate a node with as much slave detail as they have on hand.
        void printTopologyTree(std::unordered_map<uint16_t, uint16_t> const& topology_mapping,
                               std::function<std::string(uint16_t)> const& label)
        {
            std::unordered_map<uint16_t, std::vector<uint16_t>> children;
            std::vector<uint16_t> roots;
            for (auto const& [child, parent] : topology_mapping)
            {
                if (child == parent)
                {
                    roots.push_back(child);
                }
                else
                {
                    children[parent].push_back(child);
                }
            }

            // The parent map is unordered: sort so a given bus always prints the same tree,
            // branches ascending by station address (i.e. in discovery order).
            std::sort(roots.begin(), roots.end());
            for (auto& branch : children)
            {
                std::sort(branch.second.begin(), branch.second.end());
            }

            std::function<void(uint16_t, std::string const&, bool)> printNode =
                [&](uint16_t node, std::string const& prefix, bool last)
            {
                char const* branch = "├──";
                char const* continuation = "│   ";
                if (last)
                {
                    branch = "└──";
                    continuation = "    ";
                }

                printf("%s%s %s\n", prefix.c_str(), branch, label(node).c_str());

                auto it = children.find(node);
                if (it == children.end())
                {
                    return;
                }

                std::string next_prefix = prefix + continuation;
                for (size_t i = 0; i < it->second.size(); ++i)
                {
                    printNode(it->second[i], next_prefix, i + 1 == it->second.size());
                }
            };

            printf("\n Master\n");
            for (size_t i = 0; i < roots.size(); ++i)
            {
                printNode(roots[i], " ", i + 1 == roots.size());
            }
        }
    }


    void print(std::unordered_map<uint16_t, uint16_t> const& topology_mapping)
    {
        printTopologyTree(topology_mapping, [](uint16_t address)
        {
            return "Slave " + std::to_string(address);
        });
    }


    void print(std::unordered_map<uint16_t, uint16_t> const& topology_mapping, std::vector<Slave> const& slaves)
    {
        printTopologyTree(topology_mapping, [&slaves](uint16_t address)
        {
            std::string node = "Slave " + std::to_string(address);

            auto it = std::find_if(slaves.begin(), slaves.end(),
                [address](Slave const& slave) { return slave.address == address; });
            if (it == slaves.end())
            {
                return node;
            }

            node += "  " + it->name();
            auto type = it->type();
            if (not type.empty())
            {
                node += " [" + type + "]";
            }
            return node;
        });
    }



    char const* fmmuTypeToString(uint8_t fmmu_type)
    {
        // see ETG2010_S_R_v1i0i0_EtherCATSIISpecification
        switch (fmmu_type)
        {
            case 1:  {return "Outputs (Master to Slave)";}
            case 2:  {return "Inputs  (Slave to Master)";}
            case 3:  {return "SyncM Status (Read Mailbox)";}
            default: {return "Unused";}
        }
    }


    char const* typeToString(uint8_t esc_type)
    {
        switch (esc_type)
        {
            case 0x01: { return "First terminals"; }
            case 0x02: { return "ESC10, ESC20";    }
            case 0x03: { return "First EK1100";    }
            case 0x04: { return "IP Core";         }
            case 0x05: { return "Internal FPGA";   }
            case 0x11: { return "ET1100";          }
            case 0x12: { return "ET1200";          }
            case 0x91: { return "TMS320F2838x";    }
            case 0x98: { return "XMC4800";         }
            case 0xc0: { return "LAN9252";         }
            default:   { return "Unknown";         }
        }
    }

    char const* portToString(uint8_t esc_port_desc)
    {
        switch (esc_port_desc & 0x3)
        {
            case 1:   { return "Not configured (SII EEPROM)"; }
            case 2:   { return "EBUS";                        }
            case 3:   { return "MII";                         }
            default:  { return "Not implemented";             }
        }
    }

    std::string featuresToString(uint16_t esc_features)
    {
        std::string features;

        features += " - FMMU:                            ";
        if (esc_features & ESC::feature::FMMU_BYTE_ORIENTED)
        {
            features += "Byte-oriented\n";
        }
        else
        {
            features += "Bit-oriented\n";
        }

        features += " - Unused register access:          ";
        if (esc_features & ESC::feature::UNUSED_REG_ACCESS)
        {
            features += "not supported\n";
        }
        else
        {
            features += "allowed\n";
        }

        features += " - Distributed clocks:              ";
        if (esc_features & ESC::feature::DC_AVAILABLE)
        {
            features += "available\n";
        }
        else
        {
            features += "not available\n";
        }

        features += " - Distributed clocks (width):      ";
        if (esc_features & ESC::feature::DC_64_BITS)
        {
            features += "64 bits\n";
        }
        else
        {
            features += "32 bits\n";
        }

        features += " - Low jitter EBUS:                 ";
        if (esc_features & ESC::feature::EBUS_LOW_JITTER)
        {
            features += "available, jitter minimized\n";
        }
        else
        {
            features += "not available, standard jitter\n";
        }

        features += " - Enhanced Link Detection EBUS:    ";
        if (esc_features & ESC::feature::EBUS_ENHANCED_LINK_DETECTION)
        {
            features += "available\n";
        }
        else
        {
            features += "not available\n";
        }

        features += " - Enhanced Link Detection MII:     ";
        if (esc_features & ESC::feature::MII_ENHANCED_LINK_DETECTION)
        {
            features += "available\n";
        }
        else
        {
            features += "not available\n";
        }

        features += " - Separate handling of FCS errors: ";
        if (esc_features & ESC::feature::FCS_ERROR_SEPARATE_HANDLING)
        {
            features += "available\n";
        }
        else
        {
            features += "not available\n";
        }

        features += " - Enhanced DC SYNC Activation:     ";
        if (esc_features & ESC::feature::DC_ENHANCED_SYNC_ACTIVATION)
        {
            features += "available\n";
        }
        else
        {
            features += "not available\n";
        }

        features += " - EtherCAT LRW support:            ";
        if (esc_features & ESC::feature::ECAT_LRW)
        {
            features += "not available\n";
        }
        else
        {
            features += "available\n";
        }

        features += " - EtherCAT read/write support:     ";
        if (esc_features & ESC::feature::ECAT_B_A_F_RW)
        {
            features += "not available\n";
        }
        else
        {
            features += "available\n";
        }

        features += " - Fixed FMMU/SM configuration:     ";
        if (esc_features & ESC::feature::FIXED_FMMU_SYNC_CONF)
        {
            features += "fixed\n";
        }
        else
        {
            features += "variable\n";
        }
        return features;
    }
}
