#include "Prints.h"

#include <iomanip>

namespace kickcat
{
    void printInfo(Slave const& slave)
    {
        std::stringstream os;
        os << "\n -*-*-*-*- slave " << std::to_string(slave.address) << " -*-*-*-*-\n";
        os << "Vendor ID:       " << "0x" << std::setfill('0') << std::setw(8) << std::hex << slave.sii.vendor_id << "\n";
        os << "Product code:    " << "0x" << std::setfill('0') << std::setw(8) << std::hex << slave.sii.product_code << "\n";
        os << "Revision number: " << "0x" << std::setfill('0') << std::setw(8) << std::hex << slave.sii.revision_number << "\n";
        os << "Serial number:   " << "0x" << std::setfill('0') << std::setw(8) << std::hex << slave.sii.serial_number << "\n";
        os << "mailbox in:  size " << std::dec << slave.mailbox.recv_size << " - offset " << "0x" << std::setfill('0')
            << std::setw(4) << std::hex << slave.mailbox.recv_offset << "\n";

        os << "mailbox out: size " << std::dec << slave.mailbox.send_size << " - offset " << "0x" << std::setfill('0')
            << std::setw(4) << std::hex << slave.mailbox.send_offset << "\n";

        os << "supported mailbox protocol: " << "0x" << std::setfill('0') << std::setw(2)
            << std::hex << slave.sii.supported_mailbox<< "\n";

        os << "EEPROM: size: " << std::dec << slave.sii.eeprom_size << " - version "<< "0x" << std::setfill('0')
            << std::setw(2) << std::hex << slave.sii.eeprom_version << "\n";

        os << "\nSII size: " << std::dec << slave.sii.eeprom_size * sizeof(uint32_t) << "\n";

        for (size_t i = 0; i < slave.sii.fmmus.size(); ++i)
        {
            os << "FMMU[" << std::to_string(i) << "] " << fmmuTypeToString(slave.sii.fmmus[i]) << "\n";
        }

        for (size_t i = 0; i < slave.sii.syncManagers.size(); ++i)
        {
            auto const& sm = slave.sii.syncManagers[i];
            os << "SM[" << std::dec << i << "] config\n";
            os << "     physical address: " << "0x" << std::hex << sm->start_adress << "\n";
            os << "     length:           " << std::dec << sm->length << "\n";
            os << "     type:             " << std::dec << toString(static_cast<SyncManagerType>(sm->type)) << "\n";
            os << "     control:          " << std::hex << (int)sm->control_register << "\n";
        }

        printf("%s", os.str().c_str());
    }

    void printPDOs(Slave const& slave)
    {
        std::stringstream os;
        if (not slave.sii.RxPDO.empty())
        {
            os <<"RxPDO\n";
            for (size_t i = 0; i < slave.sii.RxPDO.size(); ++i)
            {
                auto const& pdo = slave.sii.RxPDO[i];
                auto const& name = slave.sii.strings[pdo->name];
                os << "    (0x" << std::setfill('0') << std::setw(4) << std::hex << pdo->index <<
                    " ; 0x" << std::setfill('0') << std::setw(2) << std::hex << static_cast<uint16_t>(pdo->subindex) <<
                    ") - " << std::to_string(pdo->bitlen) << " bit(s) - " << std::string(name) << "\n";
            }
        }

        if (not slave.sii.TxPDO.empty())
        {
            os << "TxPDO\n";
            for (size_t i = 0; i < slave.sii.TxPDO.size(); ++i)
            {
                auto const& pdo = slave.sii.TxPDO[i];
                auto const& name = slave.sii.strings[pdo->name];
                os << "    (0x" << std::setfill('0') << std::setw(4) << std::hex << pdo->index <<
                    " ; 0x" << std::setfill('0') << std::setw(2) << std::hex << static_cast<uint16_t>(pdo->subindex) <<
                    ") - " << std::to_string(pdo->bitlen) << " bit(s) - " << std::string(name) << "\n";
            }
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


    void print(std::unordered_map<uint16_t, uint16_t> const& topology_mapping)
    {
        printf( "\n -*-*-*-*- Topology -*-*-*-*-\n" );
        for (auto const& it : topology_mapping)
        {
            if (it.first != it.second)
            {
                printf( "Slave %04x parent : slave %04x \n", it.first, it.second);
            }
            else
            {
                printf( "Slave %04x parent : master \n", it.first);
            }
        }
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
        if (esc_features & (1 << 0))
        {
            features += "Byte-oriented\n";
        }
        else
        {
            features += "Bit-oriented\n";
        }

        features += " - Unused register access:          ";
        if (esc_features & (1 << 1))
        {
            features += "not supported\n";
        }
        else
        {
            features += "allowed\n";
        }

        features += " - Distributed clocks:              ";
        if (esc_features & (1 << 2))
        {
            features += "available\n";
        }
        else
        {
            features += "not available\n";
        }

        features += " - Distributed clocks (width):      ";
        if (esc_features & (1 << 3))
        {
            features += "64 bits\n";
        }
        else
        {
            features += "32 bits\n";
        }

        features += " - Low jitter EBUS:                 ";
        if (esc_features & (1 << 4))
        {
            features += "available, jitter minimized\n";
        }
        else
        {
            features += "not available, standard jitter\n";
        }

        features += " - Enhanced Link Detection EBUS:    ";
        if (esc_features & (1 << 5))
        {
            features += "available\n";
        }
        else
        {
            features += "not available\n";
        }

        features += " - Enhanced Link Detection MII:     ";
        if (esc_features & (1 << 6))
        {
            features += "available\n";
        }
        else
        {
            features += "not available\n";
        }

        features += " - Separate handling of FCS errors: ";
        if (esc_features & (1 << 7))
        {
            features += "available\n";
        }
        else
        {
            features += "not available\n";
        }

        features += " - Enhanced DC SYNC Activation:     ";
        if (esc_features & (1 << 8))
        {
            features += "available\n";
        }
        else
        {
            features += "not available\n";
        }

        features += " - EtherCAT LRW support:            ";
        if (esc_features & (1 << 9))
        {
            features += "not available\n";
        }
        else
        {
            features += "available\n";
        }

        features += " - EtherCAT read/write support:     ";
        if (esc_features & (1 << 10))
        {
            features += "not available\n";
        }
        else
        {
            features += "available\n";
        }

        features += " - Fixed FMMU/SM configuration:     ";
        if (esc_features & (1 << 11))
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
