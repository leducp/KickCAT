#include "Prints.h"

#include <iomanip>

namespace kickcat
{
    void printInfo(Slave const& slave)
    {
        std::stringstream os;
        os << "\n -*-*-*-*- slave " << std::to_string(slave.address) << " -*-*-*-*-\n";
        os << "Vendor ID:       " << "0x" << std::setfill('0') << std::setw(8) << std::hex << slave.vendor_id << "\n";
        os << "Product code:    " << "0x" << std::setfill('0') << std::setw(8) << std::hex << slave.product_code << "\n";
        os << "Revision number: " << "0x" << std::setfill('0') << std::setw(8) << std::hex << slave.revision_number << "\n";
        os << "Serial number:   " << "0x" << std::setfill('0') << std::setw(8) << std::hex << slave.serial_number << "\n";
        os << "mailbox in:  size " << std::dec << slave.mailbox.recv_size << " - offset " << "0x" << std::setfill('0')
            << std::setw(4) << std::hex << slave.mailbox.recv_offset << "\n";

        os << "mailbox out: size " << std::dec << slave.mailbox.send_size << " - offset " << "0x" << std::setfill('0')
            << std::setw(4) << std::hex << slave.mailbox.send_offset << "\n";

        os << "supported mailbox protocol: " << "0x" << std::setfill('0') << std::setw(2)
            << std::hex << slave.supported_mailbox << "\n";

        os << "EEPROM: size: " << std::dec << slave.eeprom_size << " - version "<< "0x" << std::setfill('0')
            << std::setw(2) << std::hex << slave.eeprom_version << "\n";

        os << "\nSII size: " << std::dec << slave.sii.buffer.size() * sizeof(uint32_t) << "\n";

        for (size_t i = 0; i < slave.sii.fmmus_.size(); ++i)
        {
            os << "FMMU[" << std::to_string(i) << "] " << fmmuTypeToString(slave.sii.fmmus_[i]) << "\n";
        }

        for (size_t i = 0; i < slave.sii.syncManagers_.size(); ++i)
        {
            auto const& sm = slave.sii.syncManagers_[i];
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


    std::string fmmuTypeToString(uint8_t fmmu_type)
    {
        // see ETG2010_S_R_v1i0i0_EtherCATSIISpecification
        switch (fmmu_type)
        {
            case 0:  {return "Unused";}
            case 1:  {return "Outputs (Slave to Master)";}
            case 2:  {return "Inputs  (Master to Slave)";}
            case 3:  {return "SyncM Status (Read Mailbox)";}
            default: {return "Unused";}
        }
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
}
