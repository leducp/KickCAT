#ifndef KICKCAT_SLAVE_H
#define KICKCAT_SLAVE_H

#include <vector>
#include <string_view>

#include "protocol.h"

namespace kickcat
{
    struct Slave
    {
        uint16_t address;

        uint32_t vendor_id;
        uint32_t product_code;
        uint32_t revision_number;
        uint32_t serial_number;

        struct Mailbox
        {
            uint16_t recv_offset;
            uint16_t recv_size;
            uint16_t send_offset;
            uint16_t send_size;

            bool can_read;  // data available on the slave
            bool can_write; // free space for a new message on the slave
            uint8_t counter; // sessionhandle, from 1 to 7
        };
        Mailbox mailbox;
        Mailbox mailbox_bootstrap;
        eeprom::MailboxProtocol supported_mailbox;

        uint32_t eeprom_size; // in bytes
        uint16_t eeprom_version;

        struct SII
        {
            std::vector<uint32_t> buffer;
            std::vector<std::string_view> strings;
            eeprom::GeneralEntry const* general;
            std::vector<uint8_t> fmmus_;
            std::vector<eeprom::SyncManagerEntry const*> syncManagers_;
            std::vector<eeprom::PDOEntry const*> RxPDO;
            std::vector<eeprom::PDOEntry const*> TxPDO;

        };
        SII sii;

        void parseSII();
        void parseStrings(uint8_t const* section_start);
        void parseFMMU(uint8_t const* section_start, uint16_t section_size);
        void parseSyncM(uint8_t const* section_start, uint16_t section_size);
        void parsePDO(uint8_t const* section_start, std::vector<eeprom::PDOEntry const*>& pdo);

        void printInfo() const;
        void printPDOs() const;

        struct PIMapping
        {
            uint8_t* data;          // buffer client to read or write back
            int32_t size;           // size fo the mapping (in bits)
            int32_t bsize;          // size of the mapping (in bytes)
            int32_t sync_manager;   // associated Sync manager
            int32_t offset;         // frame offset
        };
        PIMapping input;  // slave to master
        PIMapping output;
    };
}

#endif
