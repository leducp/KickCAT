#include "protocol.h"

namespace kickcat
{
    std::string_view CoE::SDO::abort_to_str(uint32_t abort_code)
    {
        switch (abort_code)
        {
            case 0x05030000: { return "Toggle bit not changed";                                                                     }
            case 0x05040000: { return "SDO protocol timeout";                                                                       }
            case 0x05040001: { return "Client/Server command specifier not valid or unknown";                                       }
            case 0x05040005: { return "Out of memory";                                                                              }
            case 0x06010000: { return "Unsupported access to object";                                                               }
            case 0x06010001: { return "Attempt to read a write only object ";                                                       }
            case 0x06010002: { return "Attempt to write a read only object";                                                        }
            case 0x06010003: { return "Subindex cannot be written, SI0 must be 0 for write access";                                 }
            case 0x06010004: { return "SDO Complete access not supported for objects of variable length";                           }
            case 0x06010005: { return "Object length exceeds mailbox size";                                                         }
            case 0x06010006: { return "Object mapped to RxPDO, SDO Download blocked";                                               }
            case 0x06020000: { return "The object does not exist in the object dictionnary";                                        }
            case 0x06040041: { return "The object cannot be mapped into the PDO";                                                   }
            case 0x06040042: { return "The number and length of the objects to be mapped would exceed the PDO lenght";              }
            case 0x06040043: { return "General parameter incompatibility reason";                                                   }
            case 0x06040047: { return "General internal incompatibility in the device";                                             }
            case 0x06060000: { return "Access failed due to a hardware error";                                                      }
            case 0x06070010: { return "Data type does not match, length of service parameter does not match";                       }
            case 0x06070012: { return "Data type does not match, length of service parameter too high";                             }
            case 0x06070013: { return "Data type does not match, length of service parameter too low";                              }
            case 0x06090011: { return "Subindex does not exist";                                                                    }
            case 0x06090030: { return "Value range of parameter exceeded";                                                          }
            case 0x06090031: { return "Value of parameter written too high";                                                        }
            case 0x06090032: { return "Value of parameter written too low";                                                         }
            case 0x06090036: { return "Maximum value is less than minimum value";                                                   }
            case 0x08000000: { return "General error";                                                                              }
            case 0x08000020: { return "Data cannot be transferred or stored to the application";                                    }
            case 0x08000021: { return "Data cannot be transferred or stored to the application because of local control";           }
            case 0x08000022: { return "Data cannot be transferred or stored to the application because of the present device state";}
            case 0x08000023: { return "Object dictionnary dynamic generation fails or no object dictionnary is present";            }

            default:
            {
                return "unknown SDO Abort code";
            }
        }
    }
}
