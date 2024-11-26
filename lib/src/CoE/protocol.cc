#include "protocol.h"
#include "CoE/protocol.h"
#include "Error.h"
#include <sstream>

namespace kickcat::CoE
{
    char const* SDO::abort_to_str(uint32_t abort_code)
    {
        using namespace abort;
        switch (abort_code)
        {
            case TOGGLE_BIT_NOT_ALTERNATED:     { return "Toggle bit not changed";                                                                     }
            case SDO_PROTOCOL_TIMEOUT:          { return "SDO protocol timeout";                                                                       }
            case COMMAND_SPECIFIER_INVALID:     { return "Client/Server command specifier not valid or unknown";                                       }
            case INVALID_BLOCK_SIZE:            { return "Invalid block size (block mode only)";                                                       }
            case INVALID_SEQUENCE_NUMBER:       { return "Invalid sequence number (block mode only)";                                                  }
            case CRC_ERROR:                     { return "CRC error (block mode only)";                                                                }
            case OUT_OF_MEMORY:                 { return "Out of memory";                                                                              }
            case UNSUPPORTED_ACCESS:            { return "Unsupported access to object";                                                               }
            case READ_ONLY_ACCESS:              { return "Attempt to read a write only object ";                                                       }
            case WRITE_ONLY_ACCESS:             { return "Attempt to write a read only object";                                                        }
            case SUBINDEX0_CANNOT_BE_WRITTEN:   { return "Subindex cannot be written, SI0 must be 0 for write access";                                 }
            case COMPLETE_ACCESS_UNSUPPORTED:   { return "SDO Complete access not supported for objects of variable length";                           }
            case OBJECT_TOO_BIG:                { return "Object length exceeds mailbox size";                                                         }
            case OBJECT_MAPPED:                 { return "Object mapped to RxPDO, SDO Download blocked";                                               }
            case OBJECT_DOES_NOT_EXIST:         { return "The object does not exist in the object dictionnary";                                        }
            case OBJECT_CANNOT_BE_MAPPED:       { return "The object cannot be mapped into the PDO";                                                   }
            case PDO_LENGTH_EXCEEDED:           { return "The number and length of the objects to be mapped would exceed the PDO lenght";              }
            case PARAMETER_INCOMPATIBILITY:     { return "General parameter incompatibility reason";                                                   }
            case INTERNAL_INCOMPATIBILITY:      { return "General internal incompatibility in the device";                                             }
            case HARDWARE_ERROR:                { return "Access failed due to a hardware error";                                                      }
            case DATA_TYPE_LENGTH_MISMATCH:     { return "Data type does not match, length of service parameter does not match";                       }
            case DATA_TYPE_LENGTH_TOO_HIGH:     { return "Data type does not match, length of service parameter too high";                             }
            case DATA_TYPE_LENGTH_TOO_LOW:      { return "Data type does not match, length of service parameter too low";                              }
            case SUBINDEX_DOES_NOT_EXIST:       { return "Subindex does not exist";                                                                    }
            case VALUE_RANGE_EXCEEDED:          { return "Value range of parameter exceeded";                                                          }
            case VALUE_TOO_HIGH:                { return "Value of parameter written too high";                                                        }
            case VALUE_TOO_LOW:                 { return "Value of parameter written too low";                                                         }
            case MODULE_LIST_MISMATCH:          { return "Configured module list does not match detected module list";                                 }
            case MAX_LESS_THAN_MIN:             { return "Maximum value is less than minimum value";                                                   }
            case RESSOURCE_UNAVAILABLE:         { return "Resource not available: SDO connection";                                                     }
            case GENERAL_ERROR:                 { return "General error";                                                                              }
            case TRANSFER_ABORTED_GENERIC:      { return "Data cannot be transferred or stored to the application";                                    }
            case TRANSFER_ABORTED_LOCAL_CTRL:   { return "Data cannot be transferred or stored to the application because of local control";           }
            case TRANSFER_ABORTED_ESM_STATE:    { return "Data cannot be transferred or stored to the application because of the present device state";}
            case DICTIONARY_GENERTION_FAILURE:  { return "Object dictionnary dynamic generation fails or no object dictionnary is present";            }
            case NO_DATA_AVAILABLE:             { return "No data available";                                                                          }

            default:
            {
                return "unknown SDO Abort code";
            }
        }
    }


    std::string toString(CoE::Header const* header)
    {
        switch (header->service)
        {
            case Service::SDO_REQUEST:
            {
                auto* sdo = pointData<CoE::ServiceData>(header);

                std::stringstream result;

                result << "SDO (" << std::hex << sdo->index << "." << std::dec << (int32_t)sdo->subindex << ")\n";
                result << "  service: request\n";
                result << "  command: " << CoE::SDO::request::toString(sdo->command) << '\n';

                return result.str();
            }

            case Service::SDO_RESPONSE:
            case Service::EMERGENCY:
            case Service::TxPDO:
            case Service::RxPDO:
            case Service::TxPDO_REMOTE_REQUEST:
            case Service::RxPDO_REMOTE_REQUEST:
            case Service::SDO_INFORMATION:
            {
                return "Not implemented";
            }
            default:
            {
                return "Unknwon CoE service";
            }
        }
    }

    namespace SDO::request
    {
        char const* toString(uint8_t command)
        {
            switch (command)
            {
                case DOWNLOAD_SEGMENTED: { return "download segmented"; }
                case DOWNLOAD:           { return "download";           }
                case UPLOAD:             { return "upload";             }
                case UPLOAD_SEGMENTED:   { return "upload segmented";   }
                case ABORT:              { return "abort";              }
                default:                 { return "Unknown command";    }
            }
        }
    }
}
