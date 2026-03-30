#include "SIIParser.h"
#include "debug.h"

#include <cstring>

namespace kickcat::eeprom
{
    void parseStrings(SII& sii, uint8_t const* section_start)
    {
        sii.strings.push_back(std::string());  // index 0 is an empty string
        uint8_t const* pos = section_start;

        uint8_t number_of_strings = *pos;
        pos += 1;

        for (int32_t i = 0; i < number_of_strings; ++i)
        {
            uint8_t len = *pos;
            pos += 1;

            sii.strings.emplace_back(reinterpret_cast<char const*>(pos), len);
            pos += len;
        }
    }

    void parseFMMU(SII& sii, uint8_t const* section_start, uint16_t section_size)
    {
        for (int32_t i = 0; i < section_size; ++i)
        {
            sii.fmmus.push_back(*(section_start + i));
        }
    }

    void parseSyncM(SII& sii, uint8_t const* section_start, uint16_t section_size)
    {
        uint8_t const* pos = section_start;
        uint8_t const* end = section_start + section_size;

        while (pos < end)
        {
            SyncManagerEntry entry;
            std::memcpy(&entry, pos, sizeof(SyncManagerEntry));
            sii.syncManagers.push_back(entry);
            pos += sizeof(SyncManagerEntry);
        }
    }

    void parsePDO(uint8_t const* section_start, std::vector<PDOMapping>& pdos)
    {
        uint8_t const* pos = section_start;

        PDOMapping mapping{};
        std::memcpy(&mapping.index, pos, sizeof(uint16_t));
        pos += 2;

        uint8_t number_of_entries = *pos;
        pos += 1;

        mapping.sync_manager = *pos;
        pos += 1;

        mapping.synchronization = *pos;
        pos += 1;

        mapping.name_index = *pos;
        pos += 1;

        std::memcpy(&mapping.flags, pos, sizeof(uint16_t));
        pos += 2;

        for (int32_t i = 0; i < number_of_entries; ++i)
        {
            PDOEntry entry;
            std::memcpy(&entry, pos, sizeof(PDOEntry));
            mapping.entries.push_back(entry);
            pos += sizeof(PDOEntry);
        }

        pdos.push_back(std::move(mapping));
    }

    void SII::parse(uint8_t const* data, std::size_t size)
    {
        if (size < sizeof(InfoEntry))
        {
            THROW_ERROR("EEPROM data too small for InfoEntry header");
        }

        // Info area: direct memcpy
        std::memcpy(&info, data, sizeof(InfoEntry));

        // Clear previous category data
        strings.clear();
        general = GeneralEntry{};
        fmmus.clear();
        syncManagers.clear();
        TxPDO.clear();
        RxPDO.clear();
        dc.clear();
        dataTypes.clear();
        unknownCategories.clear();

        // Categories start at word 0x40 = byte 0x80
        std::size_t const category_start = START_CATEGORY * 2;
        if (size <= category_start + 4)
        {
            return;
        }

        uint8_t const* pos           = data + category_start;
        uint8_t const* const max_pos = data + size - 4;
        while (pos < max_pos)
        {
            uint16_t category_type;
            uint16_t category_words;
            std::memcpy(&category_type,  pos,     sizeof(uint16_t));
            std::memcpy(&category_words, pos + 2, sizeof(uint16_t));
            uint16_t category_size       = category_words * sizeof(uint16_t);
            uint8_t const* category_data = pos + 4;
            pos += (4 + category_size);

            switch (category_type)
            {
                case Category::Strings:
                {
                    parseStrings(*this, category_data);
                    break;
                }
                case Category::DataTypes:
                {
                    dataTypes.assign(category_data, category_data + category_size);
                    break;
                }
                case Category::General:
                {
                    std::memcpy(&general, category_data, sizeof(GeneralEntry));
                    break;
                }
                case Category::FMMU:
                {
                    parseFMMU(*this, category_data, category_size);
                    break;
                }
                case Category::SyncM:
                {
                    parseSyncM(*this, category_data, category_size);
                    break;
                }
                case Category::TxPDO:
                {
                    parsePDO(category_data, TxPDO);
                    break;
                }
                case Category::RxPDO:
                {
                    parsePDO(category_data, RxPDO);
                    break;
                }
                case Category::DC:
                {
                    dc.assign(category_data, category_data + category_size);
                    break;
                }
                case Category::End:
                {
                    return;
                }
                default:
                {
                    unknownCategories.push_back({category_type, {category_data, category_data + category_size}});
                }
            }
        };
    }


    namespace
    {
        template<typename T>
        void append(std::vector<uint8_t>& out, T const& value)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            auto const* bytes = reinterpret_cast<uint8_t const*>(&value);
            out.insert(out.end(), bytes, bytes + sizeof(T));
        }

        void append(std::vector<uint8_t>& out, uint8_t const* data, std::size_t size)
        {
            out.insert(out.end(), data, data + size);
        }

        void appendCategory(std::vector<uint8_t>& out, uint16_t type, uint8_t const* data, std::size_t size_bytes)
        {
            append(out, type);
            append(out, static_cast<uint16_t>(size_bytes / sizeof(uint16_t)));
            append(out, data, size_bytes);
        }
    }


    std::vector<uint8_t> SII::serialize() const
    {
        std::vector<uint8_t> out;
        out.reserve(512);

        // Info area
        InfoEntry info_copy = info;
        info_copy.crc = computeInfoCRC(info_copy);
        append(out, info_copy);

        // Strings category
        if (strings.size() > 1)
        {
            std::vector<uint8_t> strings_data;
            // strings[0] is reserved empty string, actual strings start at index 1
            uint8_t count = static_cast<uint8_t>(strings.size() - 1);
            strings_data.push_back(count);
            for (std::size_t i = 1; i < strings.size(); ++i)
            {
                strings_data.push_back(static_cast<uint8_t>(strings[i].size()));
                strings_data.insert(strings_data.end(), strings[i].begin(), strings[i].end());
            }
            // Pad to word boundary
            if (strings_data.size() % 2 != 0)
            {
                strings_data.push_back(0);
            }
            appendCategory(out, Category::Strings, strings_data.data(), strings_data.size());
        }

        // DataTypes category (raw)
        if (not dataTypes.empty())
        {
            appendCategory(out, Category::DataTypes, dataTypes.data(), dataTypes.size());
        }

        // General category
        appendCategory(out, Category::General, reinterpret_cast<uint8_t const*>(&general), sizeof(GeneralEntry));

        // FMMU category
        if (not fmmus.empty())
        {
            std::vector<uint8_t> fmmu_data(fmmus);
            if (fmmu_data.size() % 2 != 0)
            {
                fmmu_data.push_back(0);
            }
            appendCategory(out, Category::FMMU, fmmu_data.data(), fmmu_data.size());
        }

        // SyncManager category
        if (not syncManagers.empty())
        {
            std::size_t sm_size = syncManagers.size() * sizeof(SyncManagerEntry);
            appendCategory(out, Category::SyncM, reinterpret_cast<uint8_t const*>(syncManagers.data()), sm_size);
        }

        // TxPDO categories
        for (auto const& mapping : TxPDO)
        {
            std::vector<uint8_t> pdo_data;
            append(pdo_data, mapping.index);
            pdo_data.push_back(static_cast<uint8_t>(mapping.entries.size()));
            pdo_data.push_back(mapping.sync_manager);
            pdo_data.push_back(mapping.synchronization);
            pdo_data.push_back(mapping.name_index);
            append(pdo_data, mapping.flags);
            for (auto const& entry : mapping.entries)
            {
                append(pdo_data, entry);
            }
            appendCategory(out, Category::TxPDO, pdo_data.data(), pdo_data.size());
        }

        // RxPDO categories
        for (auto const& mapping : RxPDO)
        {
            std::vector<uint8_t> pdo_data;
            append(pdo_data, mapping.index);
            pdo_data.push_back(static_cast<uint8_t>(mapping.entries.size()));
            pdo_data.push_back(mapping.sync_manager);
            pdo_data.push_back(mapping.synchronization);
            pdo_data.push_back(mapping.name_index);
            append(pdo_data, mapping.flags);
            for (auto const& entry : mapping.entries)
            {
                append(pdo_data, entry);
            }
            appendCategory(out, Category::RxPDO, pdo_data.data(), pdo_data.size());
        }

        // DC category (raw)
        if (not dc.empty())
        {
            appendCategory(out, Category::DC, dc.data(), dc.size());
        }

        // Unknown/vendor-specific categories (round-trip preservation)
        for (auto const& raw : unknownCategories)
        {
            appendCategory(out, raw.type, raw.data.data(), raw.data.size());
        }

        // End marker
        append(out, static_cast<uint16_t>(Category::End));
        append(out, uint16_t{0});

        return out;
    }


    uint16_t computeInfoCRC(InfoEntry const& info)
    {
        // CRC over the first 7 words (14 bytes) of the EEPROM, per ETG2010
        // Uses the ESC CRC-8 polynomial: x^8 + x^2 + x + 1 (0x07)
        uint8_t const* data = reinterpret_cast<uint8_t const*>(&info);
        uint8_t crc = 0xFF;

        for (std::size_t i = 0; i < 14; ++i)
        {
            crc ^= data[i];
            for (int bit = 0; bit < 8; ++bit)
            {
                if (crc & 0x80)
                {
                    crc = (crc << 1) ^ 0x07;
                }
                else
                {
                    crc = crc << 1;
                }
            }
        }

        return crc;
    }
}
