#include <sstream>
#include <algorithm>
#include <streambuf>
#include <string_view>

#include "kickcat/CoE/OD.h"

namespace kickcat::CoE
{
    char const* toString(enum ObjectCode code)
    {
        switch (code)
        {
            case ObjectCode::NIL:       { return "NULL";      }
            case ObjectCode::DOMAIN:    { return "DOMAIN";    }
            case ObjectCode::DEFTYPE:   { return "DEFTYPE";   }
            case ObjectCode::DEFSTRUCT: { return "DEFSTRUCT"; }
            case ObjectCode::VAR:       { return "VAR";       }
            case ObjectCode::ARRAY:     { return "ARRAY";     }
            case ObjectCode::RECORD:    { return "RECORD";    }
            default:                    { return "Invalid";   }
        }
    }

    char const* toString(DataType type)
    {
        switch (type)
        {
            case DataType::BOOLEAN:         { return "boolean";         }
            case DataType::BYTE:            { return "byte";            }
            case DataType::WORD:            { return "word";            }
            case DataType::DWORD:           { return "dword";           }

            case DataType::BIT2:            { return "bit 2";           }
            case DataType::BIT3:            { return "bit 3";           }
            case DataType::BIT4:            { return "bit 4";           }
            case DataType::BIT5:            { return "bit 5";           }
            case DataType::BIT6:            { return "bit 6";           }
            case DataType::BIT7:            { return "bit 7";           }
            case DataType::BIT8:            { return "bit 8";           }
            case DataType::BIT9:            { return "bit 9";           }
            case DataType::BIT10:           { return "bit 10";          }
            case DataType::BIT11:           { return "bit 11";          }
            case DataType::BIT12:           { return "bit 12";          }
            case DataType::BIT13:           { return "bit 13";          }
            case DataType::BIT14:           { return "bit 14";          }
            case DataType::BIT15:           { return "bit 15";          }
            case DataType::BIT16:           { return "bit 16";          }

            case DataType::BITARR8 :        { return "bit array 8";     }
            case DataType::BITARR16:        { return "bit array 16";    }
            case DataType::BITARR32:        { return "bit array 32";    }

            case DataType::TIME_OF_DAY:     { return "time of day";     }
            case DataType::TIME_DIFFERENCE: { return "time difference"; }

            case CoE::DataType::REAL32:     { return "real 32";         }
            case CoE::DataType::REAL64:     { return "real 64";         }

            case DataType::INTEGER8 :       { return "integer 8";       }
            case DataType::INTEGER16:       { return "integer 16";      }
            case DataType::INTEGER24:       { return "integer 24";      }
            case DataType::INTEGER32:       { return "integer 32";      }
            case DataType::INTEGER40:       { return "integer 40";      }
            case DataType::INTEGER48:       { return "integer 48";      }
            case DataType::INTEGER56:       { return "integer 56";      }
            case DataType::INTEGER64:       { return "integer 64";      }

            case DataType::UNSIGNED8 :      { return "unsigned 8";      }
            case DataType::UNSIGNED16:      { return "unsigned 16";     }
            case DataType::UNSIGNED24:      { return "unsigned 24";     }
            case DataType::UNSIGNED32:      { return "unsigned 32";     }
            case DataType::UNSIGNED40:      { return "unsigned 40";     }
            case DataType::UNSIGNED48:      { return "unsigned 48";     }
            case DataType::UNSIGNED56:      { return "unsigned 56";     }
            case DataType::UNSIGNED64:      { return "unsigned 64";     }

            case DataType::VISIBLE_STRING:  { return "visible string";  }
            case DataType::OCTET_STRING:    { return "octet string ";   }
            case DataType::UNICODE_STRING:  { return "unicode string";  }
            case DataType::GUID:            { return "GUID";            }

            case DataType::ARRAY_OF_INT:    { return "array of int";    }
            case DataType::ARRAY_OF_SINT:   { return "array of sint";   }
            case DataType::ARRAY_OF_DINT:   { return "array of dint";   }
            case DataType::ARRAY_OF_UDINT:  { return "array of udint";  }

            case DataType::PDO_MAPPING:     { return "PDO Mapping";     }
            case DataType::SDO_PARAMETER:   { return "SDO Parameter";   }
            case DataType::IDENTITY:        { return "Identity";        }

            case DataType::COMMAND_PAR:             { return "CommandPar";          }
            case DataType::PDO_PARAMETER:           { return "PDO Parameter";       }
            case DataType::ENUM:                    { return "Enum";                }
            case DataType::SM_SYNCHRONISATION:      { return "SMSynchronisation";   }
            case DataType::RECORD:                  { return "Record";              }
            case DataType::BACKUP_PARAMETER:        { return "BackupParameter";     }
            case DataType::MODULAR_DEVICE_PROFILE:  { return "ModularDeviceProfile";}
            case DataType::ERROR_SETTING:           { return "ErrorSetting";        }
            case DataType::DIAGNOSIS_HISTORY:       { return "DiagnosisHistory";    }
            case DataType::EXTERNAL_SYNC_STATUS:    { return "ExternalSyncStatus";  }
            case DataType::EXTERNAL_SYNC_SETTINGS:  { return "ExternalSyncSettings";}
            case DataType::DEFTYPE_FSOEFRAME:       { return "DefTypeFSOEFrame";    }
            case DataType::DEFTYPE_FSOECOMMPAR:     { return "DefTypeFSOECommPar";  }

            default: { return "Unknown data type"; }
        }
    }

    std::string Entry::dataToString() const
    {
        std::stringstream result;
        result << "0x" << std::hex;
        switch (type)
        {
            case CoE::DataType::INTEGER8:
            case CoE::DataType::UNSIGNED8:
            case CoE::DataType::BYTE:
            case CoE::DataType::BOOLEAN:
            {
                /// StringStream take uint8_t as ASCII character. uint16 wrapper to avoid that.
                uint16_t uint8Wrapper{0};
                uint8Wrapper = *static_cast<uint8_t const *>(data);
                result << uint8Wrapper;
                break;
            }
            case CoE::DataType::INTEGER16:
            case CoE::DataType::UNSIGNED16:
            {
                result << *static_cast<int16_t const *>(data);
                break;
            }
            case CoE::DataType::INTEGER32:
            case CoE::DataType::UNSIGNED32:
            case CoE::DataType::REAL32:
            {
                result << *static_cast<uint32_t const *>(data);
                break;
            }
            case CoE::DataType::INTEGER64:
            case CoE::DataType::UNSIGNED64:
            case CoE::DataType::REAL64:
            {
                result << *static_cast<int64_t const *>(data);
                break;
            }
            case CoE::DataType::VISIBLE_STRING:
            {
                char const* rawString = static_cast<char const*>(data);
                std::string_view strResult{rawString, strnlen(rawString, bitlen/8)};
                return std::string(strResult);
            }
            default: {}
        }

        return result.str();
    }


    std::string toString(Object const& object)
    {
        std::stringstream result;

        result << "Object 0x" << std::hex << object.index << std::dec << '\n';
        result << "  Name:         " << object.name << '\n';
        result << "  Code:         " << toString(object.code) << '\n';
        result << "  Max subindex: " << object.entries.size() << '\n';

        for (auto const& entry : object.entries)
        {
            result << "  * Subindex " << (int)entry.subindex << '\n';
            result << "      Desc:   " << entry.description << '\n';
            result << "      Type:   " << toString(entry.type) << '\n';
            result << "      Bitlen: " << entry.bitlen << '\n';
            result << "      Access: " << Access::toString(entry.access) << '\n';

            result << "      Data:   ";
            if (entry.data == nullptr)
            {
                result << "nullptr\n";
            }
            else
            {
                switch (entry.type)
                {
                    case DataType::BYTE:        { result << (int)*static_cast<uint8_t*> (entry.data); break; }
                    case DataType::INTEGER8:    { result << (int)*static_cast<int8_t*>  (entry.data); break; }
                    case DataType::UNSIGNED8:   { result << (int)*static_cast<uint8_t*> (entry.data); break; }
                    case DataType::INTEGER16:   { result << *static_cast<int16_t*> (entry.data); break; }
                    case DataType::UNSIGNED16:  { result << *static_cast<uint16_t*>(entry.data); break; }
                    case DataType::INTEGER32:   { result << *static_cast<int32_t*> (entry.data); break; }
                    case DataType::UNSIGNED32:  { result << *static_cast<uint32_t*>(entry.data); break; }
                    case DataType::INTEGER64:   { result << *static_cast<int64_t*> (entry.data); break; }
                    case DataType::UNSIGNED64:  { result << *static_cast<uint64_t*>(entry.data); break; }
                    case DataType::REAL64:      { result << *static_cast<double*>  (entry.data); break; }
                    case DataType::REAL32:      { result << *static_cast<float*>   (entry.data); break; }
                    default:                    { result << "no rendered"; }
                }
            }
            result << '\n';
        }

        return result.str();
    }


    std::string Access::toString(uint16_t access)
    {
        std::string result = "";
        if (access == 0)
        {
            return result;
        }

        if (access & Access::READ)
        {
            result += "read(";
            if (access & Access::READ_PREOP)  { result += "PreOP,";  }
            if (access & Access::READ_SAFEOP) { result += "SafeOP,"; }
            if (access & Access::READ_OP)     { result += "OP,";     }
            result.back() = ')';
            result += ", ";
        }

        if (access & Access::WRITE)
        {
            result += "write(";
            if (access & Access::WRITE_PREOP)  { result += "PreOP,";  }
            if (access & Access::WRITE_SAFEOP) { result += "SafeOP,"; }
            if (access & Access::WRITE_OP)     { result += "OP,";     }
            result.back() = ')';
            result += ", ";
        }

        if (access & Access::RxPDO)   { result += "RxPDO, ";   }
        if (access & Access::TxPDO)   { result += "RxPDO, ";   }
        if (access & Access::BACKUP)  { result += "Backup, ";  }
        if (access & Access::SETTING) { result += "Setting, "; }

        // remove last ", "
        result.pop_back();
        result.pop_back();

        return result;
    }

    Entry::Entry(uint8_t subindex_in, uint16_t bitlen_in, uint16_t access_in, DataType type_in, std::string const& description_in)
        : subindex{subindex_in}
        , bitlen{bitlen_in}
        , access{access_in}
        , type{type_in}
        , description{description_in}
        , data{nullptr}
    {

    }


    Entry::~Entry()
    {
        if (data != nullptr)
        {
            std::free(data);
        }
    }


    Entry::Entry(Entry&& other)
    {
        *this = std::move(other);
    }

    Entry& Entry::operator=(Entry&& other)
    {
        subindex    = std::move(other.subindex);
        bitlen      = std::move(other.bitlen);
        access      = std::move(other.access);
        type        = std::move(other.type);
        description = std::move(other.description);
        data        = std::move(other.data);
        other.data = nullptr;

        return *this;
    }


    std::tuple<Object*, Entry*> findObject(std::shared_ptr<Dictionary> dict, uint16_t index, uint8_t subindex)
    {
        auto object_it = std::find_if(dict->begin(), dict->end(), [index](Object const& object)
        {
            return (object.index == index);
        });

        if (object_it == dict->end())
        {
            return {nullptr, nullptr};
        }

        auto entry_it = std::find_if(object_it->entries.begin(), object_it->entries.end(), [subindex](Entry const& entry)
        {
            return (entry.subindex == subindex);
        });

        if (entry_it == object_it->entries.end())
        {
            return {&(*object_it), nullptr};
        }

        return {&(*object_it), &(*entry_it)};
    }
}
