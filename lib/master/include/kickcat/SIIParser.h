#ifndef KICKCAT_SII_PARSER_H
#define KICKCAT_SII_PARSER_H

#include <vector>
#include "kickcat/protocol.h"

namespace kickcat::eeprom
{
    // see ETG2010_S_R_V1.0.1 SII Specification
    struct SII
    {
        std::vector<uint32_t> eeprom;

        // Identity
        uint32_t vendor_id;
        uint32_t product_code;
        uint32_t revision_number;
        uint32_t serial_number;

        // Bootstrap Mailbox
        uint16_t mailboxBootstrap_recv_offset;
        uint16_t mailboxBootstrap_recv_size;
        uint16_t mailboxBootstrap_send_offset;
        uint16_t mailboxBootstrap_send_size;

        // Mailbox
        uint16_t mailbox_recv_offset;
        uint16_t mailbox_recv_size;
        uint16_t mailbox_send_offset;
        uint16_t mailbox_send_size;
        eeprom::MailboxProtocol supported_mailbox;

        // Size
        uint32_t eeprom_size;  // in bytes
        uint16_t eeprom_version;

        // Categories
        std::vector<std::string_view> strings;
        eeprom::GeneralEntry const* general;
        std::vector<uint8_t> fmmus;
        std::vector<eeprom::SyncManagerEntry const*> syncManagers;
        std::vector<eeprom::PDOEntry const*> TxPDO;
        std::vector<eeprom::PDOEntry const*> RxPDO;

        void parse();
    };
}

#endif

