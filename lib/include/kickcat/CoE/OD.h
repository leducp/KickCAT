#ifndef KICKCAT_COE_OD_H
#define KICKCAT_COE_OD_H

#include <vector>
#include <string>
#include <cstdint>
#include <tuple>

namespace kickcat::CoE
{
    // ETG 1000.5 chapter 5 Data type ASE (Application Service Element) and ETG 1020 Base Data Types
    enum class ObjectCode : uint8_t
    {
        NIL       = 0x00,    // An object with no data fields
        DOMAIN    = 0x02,
        DEFTYPE   = 0x05,
        DEFSTRUCT = 0x06,
        VAR       = 0x07,
        ARRAY     = 0x08,
        RECORD    = 0x09
    };
    char const* toString(enum ObjectCode code);

    // ETG 1000.5 chapter 5 Data type ASE (Application Service Element) and ETG 1020 Base Data Types
    enum class DataType: uint16_t
    {
        UNKNOWN         = 0x0000,   // custom, to handle unsupported types by the stack
        BOOLEAN         = 0x0001,

        BYTE            = 0x001E,
        WORD            = 0x001F,
        DWORD           = 0x0020,

        BIT2            = 0x0031,
        BIT3            = 0x0032,
        BIT4            = 0x0033,
        BIT5            = 0x0034,
        BIT6            = 0x0035,
        BIT7            = 0x0036,
        BIT8            = 0x0037,
        BIT9            = 0x0038,
        BIT10           = 0x0039,
        BIT11           = 0x003A,
        BIT12           = 0x003B,
        BIT13           = 0x003C,
        BIT14           = 0x003D,
        BIT15           = 0x003E,
        BIT16           = 0x003F,

        BITARR8         = 0x002D,
        BITARR16        = 0x002E,
        BITARR32        = 0x002F,

        TIME_OF_DAY     = 0x000C,
        TIME_DIFFERENCE = 0x000D,

        REAL32          = 0x0008,
        REAL64          = 0x0011,

        INTEGER8        = 0x0002,
        INTEGER16       = 0x0003,
        INTEGER24       = 0x0010,
        INTEGER32       = 0x0004,
        INTEGER40       = 0x0012,
        INTEGER48       = 0x0013,
        INTEGER56       = 0x0014,
        INTEGER64       = 0x0015,

        UNSIGNED8       = 0x0005,
        UNSIGNED16      = 0x0006,
        UNSIGNED24      = 0x0016,
        UNSIGNED32      = 0x0007,
        UNSIGNED40      = 0x0018,
        UNSIGNED48      = 0x0019,
        UNSIGNED56      = 0x001A,
        UNSIGNED64      = 0x001B,

        GUID            = 0x001D,

        VISIBLE_STRING  = 0x0009,   // STRING(n)
        OCTET_STRING    = 0x000A,   // ARRAY [0..n] OF BYTE
        UNICODE_STRING  = 0x000B,   // ARRAY [0..n] OF UINT
        ARRAY_OF_INT    = 0x0260,   // ARRAY [0..n] OF INT
        ARRAY_OF_SINT   = 0x0261,   // ARRAY [0..n] OF SINT
        ARRAY_OF_DINT   = 0x0262,   // ARRAY [0..n] OF DINT
        ARRAY_OF_UDINT  = 0x0263,   // ARRAY [0..n] OF UDINT

        PDO_MAPPING     = 0x0021,
        SDO_PARAMETER   = 0x0022,
        IDENTITY        = 0x0023,

        COMMAND_PAR            = 0x0025,
        PDO_PARAMETER          = 0x0027,
        ENUM                   = 0x0028,
        SM_SYNCHRONISATION     = 0x0029,
        RECORD                 = 0x002A,
        BACKUP_PARAMETER       = 0x002B,
        MODULAR_DEVICE_PROFILE = 0x002C,
        ERROR_SETTING          = 0x0281,
        DIAGNOSIS_HISTORY      = 0x0282,
        EXTERNAL_SYNC_STATUS   = 0x0283,
        EXTERNAL_SYNC_SETTINGS = 0x0284,
        DEFTYPE_FSOEFRAME      = 0x0285,
        DEFTYPE_FSOECOMMPAR    = 0x0286
    };
    char const* toString(enum DataType type);
    constexpr bool isBasic(DataType type)
    {
        switch (type)
        {
            case DataType::INTEGER8:
            case DataType::INTEGER16:
            case DataType::INTEGER32:
            case DataType::INTEGER64:
            case DataType::UNSIGNED8:
            case DataType::UNSIGNED16:
            case DataType::UNSIGNED32:
            case DataType::UNSIGNED64:
            case DataType::REAL32:
            case DataType::REAL64:
            case DataType::BOOLEAN:
            case DataType::BYTE:
            {
                return true;
            }
            default:
            {
                return false;
            }
        }
    }

    // ETG1000.5 Chapter 6.1.4.2.1 Formal model
    // ETG1000.6 Chapter 5.6.3.6.2 Get Entry Description Response
    namespace Access
    {
        constexpr uint16_t READ_PREOP   = (1 << 0);
        constexpr uint16_t READ_SAFEOP  = (1 << 1);
        constexpr uint16_t READ_OP      = (1 << 2);
        constexpr uint16_t WRITE_PREOP  = (1 << 3);
        constexpr uint16_t WRITE_SAFEOP = (1 << 4);
        constexpr uint16_t WRITE_OP     = (1 << 5);
        constexpr uint16_t RxPDO        = (1 << 6);
        constexpr uint16_t TxPDO        = (1 << 7);
        constexpr uint16_t BACKUP       = (1 << 8);
        constexpr uint16_t SETTING      = (1 << 9);

        // helpers
        constexpr uint16_t READ     = (Access::READ_PREOP  | Access::READ_SAFEOP  | Access::READ_OP);
        constexpr uint16_t WRITE    = (Access::WRITE_PREOP | Access::WRITE_SAFEOP | Access::WRITE_OP);
        constexpr uint16_t MAPPABLE = (Access::RxPDO | Access::TxPDO);

        std::string toString(uint16_t access);
    }

    struct Entry    // ETG1000.5 6.1.4.2.1 Formal model
    {
        Entry() = default;
        Entry(uint8_t subindex, uint16_t bitlen, uint16_t access,
              DataType type, std::string const& description);
        ~Entry();

        Entry(Entry const&) = delete;
        Entry& operator=(Entry const& other) = delete;

        Entry(Entry&& other);
        Entry& operator=(Entry&& other);

        uint8_t      subindex;
        uint16_t     bitlen;    // For PDO, shall be < 11888
        uint16_t     access{0};
        DataType     type;
        // default value
        // min value
        // max value
        std::string  description;

        void* data{nullptr};
    };

    struct Object   // ETG1000.5 6.1.4.2.1 Formal model
    {
        uint16_t            index;
        ObjectCode          code;
        std::string         name;
        std::vector<Entry>  entries;
    };
    std::string toString(Object const& object);

    using Dictionary = std::vector<Object>;
    std::tuple<Object*, Entry*> findObject(Dictionary& dict, uint16_t index, uint8_t subindex);

    // Singleton
    Dictionary& dictionary();
}

#endif
