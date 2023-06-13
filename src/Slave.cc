#include "Slave.h"
#include "Error.h"

#include <iomanip>

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
        uint32_t temp_word = 0;

        // Identity
        vendor_id       = sii.buffer.at(eeprom::VENDOR_ID       / sizeof(uint16_t));
        product_code    = sii.buffer.at(eeprom::PRODUCT_CODE    / sizeof(uint16_t));
        revision_number = sii.buffer.at(eeprom::REVISION_NUMBER / sizeof(uint16_t));
        serial_number   = sii.buffer.at(eeprom::SERIAL_NUMBER   / sizeof(uint16_t));

        // Mailbox info
        temp_word = sii.buffer.at((eeprom::BOOTSTRAP_MAILBOX  + eeprom::RECV_MBO_OFFSET) / sizeof(uint16_t));
        mailbox_bootstrap.recv_offset = static_cast<uint16_t>(temp_word);
        mailbox_bootstrap.recv_size   = static_cast<uint16_t>(temp_word >> 16);

        temp_word = sii.buffer.at((eeprom::BOOTSTRAP_MAILBOX  + eeprom::SEND_MBO_OFFSET) / sizeof(uint16_t));
        mailbox_bootstrap.send_offset = static_cast<uint16_t>(temp_word);
        mailbox_bootstrap.send_size   = static_cast<uint16_t>(temp_word >> 16);

        temp_word = sii.buffer.at((eeprom::STANDARD_MAILBOX  + eeprom::RECV_MBO_OFFSET) / sizeof(uint16_t));
        mailbox.recv_offset = static_cast<uint16_t>(temp_word);
        mailbox.recv_size   = static_cast<uint16_t>(temp_word >> 16);

        temp_word = sii.buffer.at((eeprom::STANDARD_MAILBOX  + eeprom::SEND_MBO_OFFSET) / sizeof(uint16_t));
        mailbox.send_offset = static_cast<uint16_t>(temp_word);
        mailbox.send_size   = static_cast<uint16_t>(temp_word >> 16);

        temp_word = sii.buffer.at(eeprom::MAILBOX_PROTOCOL / sizeof(uint16_t));
        supported_mailbox = static_cast<eeprom::MailboxProtocol>(temp_word);

        // EEPROM
        temp_word = sii.buffer.at(eeprom::EEPROM_SIZE / sizeof(uint16_t));
        eeprom_size = (temp_word & 0xFF) + 1;   // 0 means 1024 bits
        eeprom_size *= 128;                     // Kibit to bytes
        eeprom_version = static_cast<uint16_t>(temp_word >> 16);

        // Categories
        uint8_t const* pos = reinterpret_cast<uint8_t*>(sii.buffer.data()) + eeprom::START_CATEGORY * 2;
        uint8_t const* const max_pos = reinterpret_cast<uint8_t*>(sii.buffer.data() + sii.buffer.size() - 4);
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


    ErrorCounters const& Slave::errorCounters() const
    {
        return error_counters;
    }


    int Slave::computeRelativeErrorCounters()
    {
        int current_error_sum = computeErrorCounters();
        int delta_error_sum = current_error_sum - previous_errors_sum;

        previous_errors_sum = current_error_sum;
        return delta_error_sum;
    }


    bool Slave::checkAbsoluteErrorCounters(int max_absolute_errors)
    {
        return computeErrorCounters() > max_absolute_errors;
    }


    int Slave::computeErrorCounters() const
    {
        int sum = 0;
        for (int32_t i = 0; i < 4; ++i)
        {
            sum += error_counters.rx[i].invalid_frame;
            sum += error_counters.rx[i].physical_layer;
            sum += error_counters.lost_link[i];
        }

        return sum;
    }

    int Slave::countOpenPorts()
    {
        return  dl_status.PL_port0 +
                dl_status.PL_port1 +
                dl_status.PL_port2 +
                dl_status.PL_port3;
    }
}
