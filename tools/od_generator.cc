#include <fstream>
#include <memory>
#include <algorithm>
#include <argparse/argparse.hpp>

#include "kickcat/CoE/OD.h"
#include "kickcat/Prints.h"
#include "kickcat/CoE/EsiParser.h"

namespace kickcat
{
    constexpr std::string_view OD_POPULATOR_FILE{"od_populator.cc"};

    char const* toStdType(CoE::DataType dataType)
    {
        switch (dataType)
        {
            case CoE::DataType::BOOLEAN:        { return "uint8_t";     }
            case CoE::DataType::BYTE:           { return "uint8_t";     }
            case CoE::DataType::WORD:           { return "uint16_t";    }
            case CoE::DataType::DWORD:          { return "uint32_t";    }
            case CoE::DataType::BIT2:
            case CoE::DataType::BIT3:
            case CoE::DataType::BIT4:
            case CoE::DataType::BIT5:
            case CoE::DataType::BIT6:
            case CoE::DataType::BIT7:
            case CoE::DataType::BIT8:           { return "uint8_t";     }
            case CoE::DataType::UNSIGNED8:      { return "uint8_t";     }
            case CoE::DataType::UNSIGNED16:     { return "uint16_t";    }
            case CoE::DataType::UNSIGNED24:     { return "uint32_t";    }
            case CoE::DataType::UNSIGNED32:     { return "uint32_t";    }
            case CoE::DataType::UNSIGNED40:
            case CoE::DataType::UNSIGNED48:
            case CoE::DataType::UNSIGNED56:
            case CoE::DataType::UNSIGNED64:     { return "uint64_t";    }
            case CoE::DataType::INTEGER8:       { return "int8_t";      }
            case CoE::DataType::INTEGER16:      { return "int16_t";     }
            case CoE::DataType::INTEGER24:      { return "int32_t";     }
            case CoE::DataType::INTEGER32:      { return "int32_t";     }
            case CoE::DataType::INTEGER40:
            case CoE::DataType::INTEGER48:
            case CoE::DataType::INTEGER56:
            case CoE::DataType::INTEGER64:      { return "int64_t";     }
            case CoE::DataType::REAL32:         { return "float";       }
            case CoE::DataType::REAL64:         { return "double";      }
            case CoE::DataType::VISIBLE_STRING: { return "char const*"; }
            default:
            {
                std::string what = "Unsupported type ";
                what += toString(dataType);
                throw std::invalid_argument{what};
            }
        }
    }

    char const* toDataTypeString(CoE::DataType dataType)
    {
        switch (dataType)
        {
            case CoE::DataType::BOOLEAN:        { return "CoE::DataType::BOOLEAN";        }
            case CoE::DataType::BYTE:           { return "CoE::DataType::BYTE";           }
            case CoE::DataType::WORD:           { return "CoE::DataType::WORD";           }
            case CoE::DataType::DWORD:          { return "CoE::DataType::DWORD";          }
            case CoE::DataType::BIT2:           { return "CoE::DataType::BIT2";           }
            case CoE::DataType::BIT3:           { return "CoE::DataType::BIT3";           }
            case CoE::DataType::BIT4:           { return "CoE::DataType::BIT4";           }
            case CoE::DataType::BIT5:           { return "CoE::DataType::BIT5";           }
            case CoE::DataType::BIT6:           { return "CoE::DataType::BIT6";           }
            case CoE::DataType::BIT7:           { return "CoE::DataType::BIT7";           }
            case CoE::DataType::BIT8:           { return "CoE::DataType::BIT8";           }
            case CoE::DataType::UNSIGNED8:      { return "CoE::DataType::UNSIGNED8";      }
            case CoE::DataType::UNSIGNED16:     { return "CoE::DataType::UNSIGNED16";     }
            case CoE::DataType::UNSIGNED24:     { return "CoE::DataType::UNSIGNED24";     }
            case CoE::DataType::UNSIGNED32:     { return "CoE::DataType::UNSIGNED32";     }
            case CoE::DataType::UNSIGNED40:     { return "CoE::DataType::UNSIGNED40";     }
            case CoE::DataType::UNSIGNED48:     { return "CoE::DataType::UNSIGNED48";     }
            case CoE::DataType::UNSIGNED56:     { return "CoE::DataType::UNSIGNED56";     }
            case CoE::DataType::UNSIGNED64:     { return "CoE::DataType::UNSIGNED64";     }
            case CoE::DataType::INTEGER8:       { return "CoE::DataType::INTEGER8";       }
            case CoE::DataType::INTEGER16:      { return "CoE::DataType::INTEGER16";      }
            case CoE::DataType::INTEGER24:      { return "CoE::DataType::INTEGER24";      }
            case CoE::DataType::INTEGER32:      { return "CoE::DataType::INTEGER32";      }
            case CoE::DataType::INTEGER40:      { return "CoE::DataType::INTEGER40";      }
            case CoE::DataType::INTEGER48:      { return "CoE::DataType::INTEGER48";      }
            case CoE::DataType::INTEGER56:      { return "CoE::DataType::INTEGER56";      }
            case CoE::DataType::INTEGER64:      { return "CoE::DataType::INTEGER64";      }
            case CoE::DataType::REAL32:         { return "CoE::DataType::REAL32";         }
            case CoE::DataType::REAL64:         { return "CoE::DataType::REAL64";         }
            case CoE::DataType::VISIBLE_STRING: { return "CoE::DataType::VISIBLE_STRING"; }
            default:
            {
                std::string what = "Unsupported type ";
                what += toString(dataType);
                throw std::invalid_argument{what};
            }
        }
    }

    std::string toAccessString(uint16_t access)
    {
        if (access == 0)
        {
            return "0";
        }

        std::string result;
        auto append = [&](char const* name)
        {
            if (not result.empty()) { result += " | "; }
            result += name;
        };

        // Greedy: try compound shorthands first, then individual bits for leftovers
        if ((access & CoE::Access::READ) == CoE::Access::READ)
        {
            append("CoE::Access::READ");
            access &= ~CoE::Access::READ;
        }
        else
        {
            if (access & CoE::Access::READ_PREOP)  { append("CoE::Access::READ_PREOP");  access &= ~CoE::Access::READ_PREOP;  }
            if (access & CoE::Access::READ_SAFEOP) { append("CoE::Access::READ_SAFEOP"); access &= ~CoE::Access::READ_SAFEOP; }
            if (access & CoE::Access::READ_OP)     { append("CoE::Access::READ_OP");     access &= ~CoE::Access::READ_OP;     }
        }

        if ((access & CoE::Access::WRITE) == CoE::Access::WRITE)
        {
            append("CoE::Access::WRITE");
            access &= ~CoE::Access::WRITE;
        }
        else
        {
            if (access & CoE::Access::WRITE_PREOP)  { append("CoE::Access::WRITE_PREOP");  access &= ~CoE::Access::WRITE_PREOP;  }
            if (access & CoE::Access::WRITE_SAFEOP) { append("CoE::Access::WRITE_SAFEOP"); access &= ~CoE::Access::WRITE_SAFEOP; }
            if (access & CoE::Access::WRITE_OP)     { append("CoE::Access::WRITE_OP");     access &= ~CoE::Access::WRITE_OP;     }
        }

        if (access & CoE::Access::RxPDO)   { append("CoE::Access::RxPDO");   access &= ~CoE::Access::RxPDO;   }
        if (access & CoE::Access::TxPDO)   { append("CoE::Access::TxPDO");   access &= ~CoE::Access::TxPDO;   }
        if (access & CoE::Access::BACKUP)  { append("CoE::Access::BACKUP");  access &= ~CoE::Access::BACKUP;  }
        if (access & CoE::Access::SETTING) { append("CoE::Access::SETTING"); access &= ~CoE::Access::SETTING; }

        return result;
    }

    CoE::Dictionary loadOD(std::string esiFileName)
    {
        CoE::EsiParser parser;
        return parser.loadFirstDictionaryFromFile(esiFileName);
    }

    std::string addBeginning()
    {
        std::stringstream result;

        result << "/// This file is auto generated by od_generator.\n\n";

        result << "#include \"kickcat/CoE/OD.h\"\n\n";
        result << "namespace kickcat::CoE\n{\n";
        result << "    CoE::Dictionary createOD()\n    {\n";
        result << "        CoE::Dictionary dictionary;\n\n";

        return result.str();
    }

    std::string addEnding()
    {
        std::stringstream result;
        result << "         return dictionary;\n";
        result << "    }\n}\n";
        return result.str();
    }



    std::string addEntry(CoE::Entry const &entryToAdd)
    {
        if (not CoE::isBasic(entryToAdd.type))
        {
            // TODO support complex type of entry
            THROW_ERROR("Object Dictionary Generator only support basic type for now");
        }

        std::stringstream result;

        if (entryToAdd.data == nullptr)
        {
            result << "            CoE::addEntry(object,";
        }
        else
        {
            result << "            CoE::addEntry<" << toStdType(entryToAdd.type) << ">(object,";
        }

        result << std::to_string(entryToAdd.subindex) << ",";
        result << std::to_string(entryToAdd.bitlen) << ",";
        result << std::to_string(entryToAdd.bitoff) << ",";
        result << toAccessString(entryToAdd.access) << ",";
        result << toDataTypeString(entryToAdd.type) << ",";
        result << "\"" << entryToAdd.description << "\",";

        if(entryToAdd.data != nullptr and entryToAdd.type == CoE::DataType::VISIBLE_STRING)
        {
            result << "\"";
        }

        result << entryToAdd.dataToString();

        if(entryToAdd.data != nullptr and entryToAdd.type == CoE::DataType::VISIBLE_STRING)
        {
            result << "\"";
        }
        result << ");\n";

        return result.str();
    }

    std::string addObject(CoE::Object const &objectToAdd)
    {
        std::stringstream result;
        result << "        {\n";
        result << "            CoE::Object object\n";
        result << "            {\n";
        result << "                0x" << std::hex << objectToAdd.index << std::dec << ",\n";
        result << "                CoE::ObjectCode::" << CoE::toString(objectToAdd.code) << ",\n";
        result << "                \"" << objectToAdd.name << "\",\n";
        result << "                {}\n";
        result << "            };\n";

        for (auto const &entry : objectToAdd.entries)
        {
            result << addEntry(entry);
        }

        result << "            dictionary.push_back(std::move(object));\n";
        result << "        }\n\n";

        return result.str();
    }
}

int main(int argc, char *argv[])
{
    using namespace kickcat;

    argparse::ArgumentParser program("od_generator");

    std::string esi_file;
    program.add_argument("-f", "--file")
        .help("ESI XML file")
        .required()
        .store_into(esi_file);

    try
    {
        program.parse_args(argc, argv);
    }
    catch (const std::runtime_error& err)
    {
        std::cerr << err.what() << std::endl;
        std::cerr << program;
        return 1;
    }

    auto dictionary = loadOD(esi_file);

    std::ofstream f(std::string(OD_POPULATOR_FILE).c_str());

    /// Sort Dictionnary to have more readable source file
    std::sort(dictionary.begin(), dictionary.end(),
              [](CoE::Object &object1, CoE::Object &object2)
              { return object1.index < object2.index; });

    f << addBeginning();
    for (auto const &object : dictionary)
    {
        f << addObject(object);
    }
    f << addEnding();

    f.close();

    return 0;
}
