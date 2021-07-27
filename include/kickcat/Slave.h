#ifndef KICKCAT_SLAVE_H
#define KICKCAT_SLAVE_H

#include <vector>
#include <string_view>

#include "protocol.h"
#include "Mailbox.h"

namespace kickcat
{
    struct Slave
    {
        void parseSII();

        void printInfo() const;
        void printPDOs() const;
        void printErrorCounters() const;

        uint16_t address;
        uint8_t al_status{State::INVALID};
        uint16_t al_status_code;

        uint32_t vendor_id;
        uint32_t product_code;
        uint32_t revision_number;
        uint32_t serial_number;

        Mailbox mailbox;
        Mailbox mailbox_bootstrap;
        eeprom::MailboxProtocol supported_mailbox;
        int32_t waiting_datagram; // how many datagram to process for this slave

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
        SII sii{};

        struct PIMapping
        {
            uint8_t* data;          // buffer client to read or write back
            int32_t size;           // size fo the mapping (in bits)
            int32_t bsize;          // size of the mapping (in bytes)
            int32_t sync_manager;   // associated Sync manager
            uint32_t address;       // logical address
        };
        // set it to true to let user define the mapping, false to autodetect it
        // If set to true, user shall set input and output mapping bsize and sync_manager members.
        bool is_static_mapping;
        PIMapping input;            // slave to master
        PIMapping output;

        ErrorCounters error_counters;

    private:
        void parseStrings(uint8_t const* section_start);
        void parseFMMU(uint8_t const* section_start, uint16_t section_size);
        void parseSyncM(uint8_t const* section_start, uint16_t section_size);
        void parsePDO(uint8_t const* section_start, std::vector<eeprom::PDOEntry const*>& pdo);
    };
}

#endif
