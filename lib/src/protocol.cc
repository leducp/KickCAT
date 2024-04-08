#include "protocol.h"
#include "Error.h"
#include <sstream>

namespace kickcat
{
    char const* CoE::SDO::abort_to_str(uint32_t abort_code)
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

    // ETG1000.6 and ETG1020 chapter 4 Description of AL Status Codes
    char const* ALStatus_to_string(int32_t code)
    {
        switch (code)
        {
            case 0x0000:    { return "no error"; }

            case 0x0001:    { return "No error code is defined for occurred error";         }
            case 0x0002:    { return "Less hardware memory, slave needs more memory";       }
            case 0x0003:    { return "Invalid device setup";                                }
            case 0x0004:    { return "Output/Input mapping is not valid for this hardware or software revision (0x1018:03)"; }
            case 0x0006:    { return "SII/EEPROM does not match the firmware";              }
            case 0x0007:    { return "Firmware update failure";                             }
            case 0x000E:    { return "License error";                                       }

            case 0x0011:    { return "Invalid requested state change";                      }
            case 0x0012:    { return "Unknown requested state change";                      }
            case 0x0013:    { return "Boot state not supported";                            }
            case 0x0014:    { return "No valid firmware";                                   }
            case 0x0015:    { return "Invalid mailbox configuration - bootstrap";           }
            case 0x0016:    { return "Invalid mailbox configuration - safeop";              }
            case 0x0017:    { return "Invalid Sync Manager configuration";                  }
            case 0x0018:    { return "No valid inputs available";                           }
            case 0x0019:    { return "No valid outputs available";                          }
            case 0x001A:    { return "Synchronization error";                               }
            case 0x001B:    { return "Sync manager watchdog";                               }
            case 0x001C:    { return "Invalid Sync Manager Types";                          }
            case 0x001D:    { return "SM configuration for output process data is invalid"; }
            case 0x001E:    { return "SM configuration for input process data is invalid";  }
            case 0x001F:    { return "Invalid Watchdog Configuration";                      }

            case 0x0020:    { return "Slave needs cold start";          }
            case 0x0021:    { return "Slave needs INIT";                }
            case 0x0022:    { return "Slave needs PREOP";               }
            case 0x0023:    { return "Slave needs SAFEOP";              }
            case 0x0024:    { return "Invalid Input Mapping";           }
            case 0x0025:    { return "Invalid Output Mapping";          }
            case 0x0026:    { return "Inconsistent Settings";           }
            case 0x0027:    { return "Freerun not supported";           }
            case 0x0028:    { return "Synchronization not supported";   }
            case 0x0029:    { return "Freerun needs 3 Buffer Mode";     }
            case 0x002A:    { return "Background Watchdog";             }
            case 0x002B:    { return "No Valid Inputs and Outputs";     }
            case 0x002C:    { return "Fatal Sync Error";                }
            case 0x002D:    { return "SyncSignal not received";         }

            case 0x0030:    { return "Invalid DC SYNC Configuration";   }
            case 0x0031:    { return "Invalid DC Latch Configuration";  }
            case 0x0032:    { return "PLL Error";                       }
            case 0x0033:    { return "DC Sync IO Error";                }
            case 0x0034:    { return "DC Sync Timeout Error";           }
            case 0x0035:    { return "DC Invalid Sync Cycle Time";      }
            case 0x0036:    { return "DC Sync0 Cycle Time";             }
            case 0x0037:    { return "DC Sync1 Cycle Time";             }

            case 0x0041:    { return "MBX_AOE"; }
            case 0x0042:    { return "MBX_EOE"; }
            case 0x0043:    { return "MBX_COE"; }
            case 0x0044:    { return "MBX_FOE"; }
            case 0x0045:    { return "MBX_SOE"; }
            case 0x004F:    { return "MBX_VOE"; }

            case 0x0050:    { return "EEPROM No Access";            }
            case 0x0051:    { return "EEPROM access error";         }
            case 0x0052:    { return "External hardware not ready"; }

            case 0x0060:    { return "Slave Restarted Locally"; }
            case 0x0061:    { return "Device Identification Value updated"; }

            case 0x0070:    { return "Detected module ident list does not match"; }

            case 0x0080:    { return "Supply voltage too low";      }
            case 0x0081:    { return "Supply voltage too high";     }
            case 0x0082:    { return "Temperature too low";         }
            case 0x0083:    { return "Temperature too high";        }

            case 0x00F0:    { return "Application Controller available"; }

            default:        { return "Unknown error code"; }
        }
    }

    char const* toString(State state)
    {
        uint8_t raw_state = state & 0xF;
        switch (raw_state)
        {
            case INVALID:     { return "invalid";     }
            case INIT:        { return "init";        }
            case PRE_OP:      { return "pre-op";      }
            case BOOT:        { return "boot";        }
            case SAFE_OP:     { return "safe-op";     }
            case OPERATIONAL: { return "operational"; }
            default:          { return "unknown";     }
        }
    }


    char const* toString(Command cmd)
    {
        switch (cmd)
        {
            case Command::NOP : { return "No Operation";                                     }
            case Command::APRD: { return "Auto increment Physical Read";                     }
            case Command::APWR: { return "Auto increment Physical Write";                    }
            case Command::APRW: { return "Auto increment Physical Read Write";               }
            case Command::FPRD: { return "Configured address Physical Read";                 }
            case Command::FPWR: { return "Configured address Physical Write";                }
            case Command::FPRW: { return "Configured address Physical Read Write";           }
            case Command::BRD : { return "Broadcast Read";                                   }
            case Command::BWR : { return "Broadcast Write";                                  }
            case Command::BRW : { return "Broadcast Read Write";                             }
            case Command::LRD : { return "Logical memory Read";                              }
            case Command::LWR : { return "Logical memory Write";                             }
            case Command::LRW : { return "Logical memory Read Write";                        }
            case Command::ARMW: { return "Auto increment physical Read Multiple Write";      }
            case Command::FRMW: { return "Configured address Physical Read Multiple Write";  }
            default:            { return "unknown command";                                  }
        }
    }

    std::string toString(ErrorCounters const& error_counters)
    {
        std::stringstream os;
        for (int32_t i = 0; i < 4; ++i)
        {
            os << "Port " << std::to_string(i) << " \n";
            os << "  invalid frame:  " << std::to_string(error_counters.rx[i].invalid_frame) << " \n";
            os << "  physical layer: " << std::to_string(error_counters.rx[i].physical_layer) << " \n";
            os << "  forwarded:      " << std::to_string(error_counters.forwarded[i]) << " \n";
            os << "  lost link:      " << std::to_string(error_counters.lost_link[i]) << " \n";
        }
        os << " \n";
        return os.str();
    }


    std::string toString(DLStatus const& dl_status)
    {
        std::stringstream os;

        os << "PDI Operational:  "          << std::to_string(dl_status.PDI_op) << " \n";
        os << "PDI Watchdog Status : "      << std::to_string(dl_status.PDI_watchdog) << " \n";
        os << "Enhanced Link Detection :  " << std::to_string(dl_status.EL_detection) << " \n";
        os << "Reserved :  "                << std::to_string(dl_status.reserved) << " \n";

        os << "Port 0: \n";
        os << "  Physical Link :  "         << std::to_string(dl_status.PL_port0) << " \n";
        os << "  Communications : "         << std::to_string(dl_status.COM_port0) << " \n";
        os << "  Loop Function :  "         << std::to_string(dl_status.LOOP_port0) << " \n";

        os << "Port 1: \n";
        os << "  Physical Link :  "         << std::to_string(dl_status.PL_port1) << " \n";
        os << "  Communications : "         << std::to_string(dl_status.COM_port1) << " \n";
        os << "  Loop Function :  "         << std::to_string(dl_status.LOOP_port1) << " \n";

        os << "Port 2: \n";
        os << "  Physical Link :  "         << std::to_string(dl_status.PL_port2) << " \n";
        os << "  Communications : "         << std::to_string(dl_status.COM_port2) << " \n";
        os << "  Loop Function :  "         << std::to_string(dl_status.LOOP_port2) << " \n";

        os << "Port 3: \n";
        os << "  Physical Link :  "         << std::to_string(dl_status.PL_port3) << " \n";
        os << "  Communications : "         << std::to_string(dl_status.COM_port3) << " \n";
        os << "  Loop Function :  "         << std::to_string(dl_status.LOOP_port3) << " \n";

        return os.str();
    }


    std::string toString(eeprom::GeneralEntry const& general_entry)
    {
        std::stringstream os;
        os << "group_info_id: "           << std::to_string(general_entry.group_info_id) << " \n";
        os << "image_name_id: "           << std::to_string(general_entry.image_name_id) << " \n";
        os << "device_order_id: "         << std::to_string(general_entry.device_order_id) << " \n";
        os << "device_name_id: "          << std::to_string(general_entry.device_name_id) << " \n";
        os << "reserved_A: "              << std::to_string(general_entry.reserved_A) << " \n";
        os << "FoE_details: "             << std::to_string(general_entry.FoE_details) << " \n";
        os << "EoE_details: "             << std::to_string(general_entry.EoE_details) << " \n";
        os << "SoE_channels: "            << std::to_string(general_entry.SoE_channels) << " \n";
        os << "DS402_channels: "          << std::to_string(general_entry.DS402_channels) << " \n";
        os << "SysmanClass: "             << std::to_string(general_entry.SysmanClass) << " \n";
        os << "flags: "                   << std::to_string(general_entry.flags) << " \n";
        os << "current_on_ebus: "         << std::to_string(general_entry.current_on_ebus) << " \n"; // mA, negative means feeding current
        os << "group_info_id_dup: "       << std::to_string(general_entry.group_info_id_dup) << " \n";
        os << "reserved_B: "              << std::to_string(general_entry.reserved_B) << " \n";
        os << "physical_memory_address: " << std::to_string(general_entry.physical_memory_address) << " \n";
        os << "port_0: %04x "             << std::to_string(general_entry.port_0) << " \n";
        os << "port_1: %04x "             << std::to_string(general_entry.port_1) << " \n";
        os << "port_2: %04x "             << std::to_string(general_entry.port_2) << " \n";
        os << "port_3: %04x "             << std::to_string(general_entry.port_3) << " \n";
        return os.str();
    }


    std::string toString(DatagramHeader const& header)
    {
        std::stringstream os;
        os << "Header \n";
        os << "  Command :   "  << std::to_string(static_cast<uint8_t>(header.command)) << "\n";
        os << "  index :  "     << std::to_string(header.index) << "\n";
        os << "  length :  "    << std::to_string(header.len) << "\n";
        os << "  circulating  " << std::to_string(header.circulating) << "\n";
        os << "  multiple  "    << std::to_string(header.multiple) << "\n";
        os << "  IRQ  "         << std::to_string(header.irq) << "\n";
        return os.str();
    }


    char const* toString(SyncManagerType const& type)
    {
        switch (type)
        {
            case SyncManagerType::Unused:     {return "Unused";    }
            case SyncManagerType::MailboxOut: {return "MailboxOut";}
            case SyncManagerType::MailboxIn: {return "MailboxIn" ;}
            case SyncManagerType::Output:     {return "Output (Slave to Master)";    }
            case SyncManagerType::Input:      {return "Input  (Master to Slave)";     }
            default:                          {return "unknown";   }
        }
    }


    uint16_t computeWatchdogTime(nanoseconds watchdog, nanoseconds precision)
    {
        auto const wdg_time = watchdog / precision;
        if ((wdg_time < 0) or (wdg_time > UINT16_MAX))
        {
            THROW_ERROR("Invalid watchdog");
        }

        return static_cast<uint16_t>(wdg_time);
    }

    template<> mailbox::Header* pointData<mailbox::Header, uint8_t>(uint8_t* base_address)
    {
        return reinterpret_cast<mailbox::Header*>(base_address);
    }
    template<> mailbox::Header const* pointData<mailbox::Header, uint8_t>(uint8_t const* base_address)
    {
        return reinterpret_cast<mailbox::Header const*>(base_address);
    }

    template<> EthernetHeader* pointData<EthernetHeader, uint8_t>(uint8_t* base_address)
    {
        return reinterpret_cast<EthernetHeader*>(base_address);
    }
    template<> EthernetHeader const* pointData<EthernetHeader, uint8_t>(uint8_t const* base_address)
    {
        return reinterpret_cast<EthernetHeader const*>(base_address);
    }
    template<> EthernetHeader* pointData<EthernetHeader, void>(void* base_address)
    {
        return reinterpret_cast<EthernetHeader*>(base_address);
    }
    template<> EthernetHeader const* pointData<EthernetHeader, void>(void const* base_address)
    {
        return reinterpret_cast<EthernetHeader const*>(base_address);
    }

    namespace mailbox::Error
    {
        char const* toString(uint16_t code)
        {
            switch (code)
            {
                case SYNTAX:                { return "Invalid syntax (mailbox header)";         }
                case UNSUPPORTED_PROTOCOL:  { return "Protocol unsupported";                    }
                case INVALID_CHANNEL:       { return "Invalid channel value";                   }
                case SERVICE_NOT_SUPPORTED: { return "Service is not supported";                }
                case INVALID_HEADER:        { return "Invalid header (mailbox header is good)"; }
                case SIZE_TOO_SHORT:        { return "Too few received bytes";                  }
                case NO_MORE_MEMORY:        { return "Protocol cannot be processed due to limited ressources"; }
                case INVALID_SIZE:          { return "Data size is inconsistent";               }
                case SERVICE_IN_WORK:       { return "Mailbox service already in use.";         }
                default:                    { return "Unknown mailbox error code";              }
            }
        }
    }

    namespace CoE
    {
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
}
