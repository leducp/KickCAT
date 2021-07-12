#include "Slave.h"
#include "Error.h"

namespace kickcat
{
    void Slave::parseStrings(uint8_t const* section_start)
    {
        sii.strings.push_back(std::string_view()); // index 0 is an empty string
        uint8_t const* pos = section_start;

        uint8_t number_of_strings = *pos;
        pos += 1;

        for (int32_t i = 0; i < number_of_strings; ++i)
        {
            uint8_t len = *pos;
            pos += 1;

            sii.strings.push_back(std::string_view(reinterpret_cast<char const*>(pos), len));
            pos += len;
        }
    }

    void Slave::parseFMMU(uint8_t const* section_start, uint16_t section_size)
    {
        for (int32_t i = 0; i < section_size; ++i)
        {
            sii.fmmus_.push_back(*(section_start + i));
        }
    }

    void Slave::parseSyncM(uint8_t const* section_start, uint16_t section_size)
    {
        uint8_t const* pos = section_start;
        uint8_t const* end = section_start + section_size;

        while (pos < end)
        {
            sii.syncManagers_.push_back(reinterpret_cast<eeprom::SyncManagerEntry const*>(pos));
            pos += sizeof(eeprom::SyncManagerEntry);
        }
    }

    void Slave::parsePDO(uint8_t const* section_start, std::vector<eeprom::PDOEntry const*>& pdo)
    {
        uint8_t const* pos = section_start;

        //uint16_t index = *reinterpret_cast<uint16_t const*>(pos); // not used yet
        pos += 2;

        uint8_t number_of_entries = *pos;
        pos += 1;

        //uint8_t sync_manager = *pos; // not used yet
        pos +=1;

        //uint8_t synchronization = *pos; // not use yet
        pos += 1;

        //uint8_t name_index = *pos; // not used yet
        pos += 1;

        // flags (word) - unused
        pos += 2;

        for (int32_t i = 0; i < number_of_entries; ++i)
        {
            pdo.push_back(reinterpret_cast<eeprom::PDOEntry const*>(pos));
            pos += sizeof(eeprom::PDOEntry);
        }
    }

    void Slave::parseSII()
    {
        uint8_t const* pos = reinterpret_cast<uint8_t*>(sii.buffer.data());
        uint8_t const* const max_pos = reinterpret_cast<uint8_t*>(sii.buffer.data() + sii.buffer.size());

        while (pos < max_pos)
        {
            uint16_t const* category = reinterpret_cast<uint16_t const*>(pos);
            uint16_t category_size = *(category + 1) * sizeof(uint16_t);
            uint8_t const*  category_data = pos + 4;
            pos += (4 + category_size);

            switch (*category)
            {
                case eeprom::Category::Strings:
                {
                    parseStrings(category_data);
                    break;
                }
                case eeprom::Category::DataTypes:
                {
                    DEBUG_PRINT("DataTypes!\n");
                    break;
                }
                case eeprom::Category::General:
                {
                    sii.general = reinterpret_cast<eeprom::GeneralEntry const*>(category_data);
                    break;
                }
                case eeprom::Category::FMMU:
                {
                    parseFMMU(category_data, category_size);
                    break;
                }
                case eeprom::Category::SyncM:
                {
                    parseSyncM(category_data, category_size);
                    break;
                }
                case eeprom::Category::TxPDO:
                {
                    parsePDO(category_data, sii.TxPDO);
                    break;
                }
                case eeprom::Category::RxPDO:
                {
                    parsePDO(category_data, sii.RxPDO);
                    break;
                }
                case eeprom::Category::DC:
                {
                    DEBUG_PRINT("DC!\n");
                    break;
                }
                case eeprom::Category::End:
                {
                    break;
                }
                default:
                {

                }
            }
        };
    }


    void Slave::printInfo() const
    {
        printf("-*-*-*-*- slave 0x%04x -*-*-*-*-\n", address);
        printf("Vendor ID:       0x%08x\n", vendor_id);
        printf("Product code:    0x%08x\n", product_code);
        printf("Revision number: 0x%08x\n", revision_number);
        printf("Serial number:   0x%08x\n", serial_number);
        printf("mailbox in:  size %d - offset 0x%04x\n", mailbox.recv_size, mailbox.recv_offset);
        printf("mailbox out: size %d - offset 0x%04x\n", mailbox.send_size, mailbox.send_offset);
        printf("supported mailbox protocol: 0x%02x\n", supported_mailbox);
        printf("EEPROM: size: %d - version 0x%02x\n",  eeprom_size, eeprom_version);
        printf("\nSII size: %lu\n",                    sii.buffer.size() * sizeof(uint32_t));

        for (size_t i = 0; i < sii.fmmus_.size(); ++i)
        {
            printf("FMMU[%lu] %d\n", i, sii.fmmus_[i]);
        }

        for (size_t i = 0; i < sii.syncManagers_.size(); ++i)
        {
            auto const& sm = sii.syncManagers_[i];
            printf("SM[%lu] config\n", i);
            printf("     physical address: %x\n", sm->start_adress);
            printf("     length:           %d\n", sm->length);
            printf("     type:             %d\n", sm->type);
        }
    }


    void Slave::printPDOs() const
    {
        if (not sii.RxPDO.empty())
        {
            printf("RxPDO\n");
            for (size_t i = 0; i < sii.RxPDO.size(); ++i)
            {
                auto const& pdo = sii.RxPDO[i];
                auto const& name = sii.strings[pdo->name];
                printf("    (0x%04x ; 0x%02x) - %d bit(s) - %.*s\n", pdo->index, pdo->subindex, pdo->bitlen, static_cast<int>(name.size()), name.data());
            }
        }

        if (not sii.TxPDO.empty())
        {
            printf("TxPDO\n");
            for (size_t i = 0; i < sii.TxPDO.size(); ++i)
            {
                auto const& pdo = sii.TxPDO[i];
                auto const& name = sii.strings[pdo->name];
                printf("    (0x%04x ; 0x%02x) - %d bit(s) - %.*s\n", pdo->index, pdo->subindex, pdo->bitlen, static_cast<int>(name.size()), name.data());
            }
        }
    }


    void Slave::printErrorCounters() const
    {
        printf("-*-*-*-*- slave 0x%04x -*-*-*-*-\n", address);
        for (int32_t i = 0; i < 4; ++i)
        {
            printf("Port %d\n", i);
            printf("  invalid frame:  %d\n", error_counters.rx[i].invalid_frame);
            printf("  physical layer: %d\n", error_counters.rx[i].physical_layer);
            printf("  forwarded:      %d\n", error_counters.forwarded[i]);
            printf("  lost link:      %d\n", error_counters.lost_link[i]);
        }
        printf("\n");
    }
}
