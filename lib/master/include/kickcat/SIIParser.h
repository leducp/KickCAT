#ifndef KICKCAT_SII_PARSER_H
#define KICKCAT_SII_PARSER_H

#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "kickcat/protocol.h"

namespace kickcat::eeprom
{
    // see ETG2010_S_R_V1.0.1 SII Specification

    struct PDOMapping
    {
        uint16_t index;
        uint8_t  sync_manager;
        uint8_t  synchronization;
        uint8_t  name_index;
        uint16_t flags;
        std::vector<PDOEntry> entries;
    };

    struct RawCategory
    {
        uint16_t type;
        std::vector<uint8_t> data;
    };

    struct SII
    {
        InfoEntry info{};

        uint32_t eepromSizeBytes() const { return (info.size + 1) * 128; }

        // Categories (owning)
        std::vector<std::string>         strings;
        GeneralEntry                     general{};
        std::vector<uint8_t>             fmmus;
        std::vector<SyncManagerEntry>    syncManagers;
        std::vector<PDOMapping>          TxPDO;
        std::vector<PDOMapping>          RxPDO;
        std::vector<uint8_t>             dc;
        std::vector<uint8_t>             dataTypes;
        std::vector<RawCategory>         unknownCategories;

        void parse(uint8_t const* data, std::size_t size);

        /// \brief Resolve a 1-based SII string index (ETG.2010). Empty for index 0 or out-of-range.
        std::string_view getString(uint8_t index) const
        {
            if ((index == 0) or (index > strings.size())) { return {}; }
            return strings[index - 1];
        }

        template<typename T>
        void parse(std::vector<T> const& data)
        {
            parse(reinterpret_cast<uint8_t const*>(data.data()), data.size() * sizeof(T));
        }

        std::vector<uint8_t> serialize() const;
    };

    uint16_t computeInfoCRC(InfoEntry const& info);
}

#endif
