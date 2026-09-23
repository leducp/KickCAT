#include "kickcat/FoE/protocol.h"
#include "kickcat/Mailbox.h"

namespace kickcat::FoE
{
    uint32_t normalizeError(uint32_t code)
    {
        if (code <= (result::FILE_INCOMPATIBLE - result::NOT_DEFINED))
        {
            return code + result::NOT_DEFINED;
        }
        return code;
    }

    char const* errorToString(uint32_t code)
    {
        using namespace mailbox::request;
        switch (code)
        {
            case result::NOT_DEFINED:                       { return "Not defined";                          }
            case result::NOT_FOUND:                         { return "Not found";                            }
            case result::ACCESS_DENIED:                     { return "Access denied";                        }
            case result::DISK_FULL:                         { return "Disk full";                            }
            case result::ILLEGAL:                           { return "Illegal";                              }
            case result::PACKET_NUMBER_WRONG:               { return "Packet number wrong";                  }
            case result::ALREADY_EXISTS:                    { return "Already exists";                       }
            case result::NO_USER:                           { return "No user";                              }
            case result::BOOTSTRAP_ONLY:                    { return "Bootstrap only";                       }
            case result::NOT_BOOTSTRAP:                     { return "Not bootstrap";                        }
            case result::NO_RIGHTS:                         { return "No rights";                            }
            case result::PROGRAM_ERROR:                     { return "Program error";                        }
            case result::CHECKSUM_WRONG:                    { return "Checksum wrong";                       }
            case result::FIRMWARE_DOES_NOT_FIT:             { return "Firmware does not fit for hardware";   }
            case result::NO_FILE_TO_READ:                   { return "No file to read";                      }
            case result::NO_FILE_HEADER:                    { return "File header does not exist";           }
            case result::FLASH_PROBLEM:                     { return "Flash problem";                        }
            case result::FILE_INCOMPATIBLE:                 { return "File incompatible";                    }
            case MessageStatus::SUCCESS:                    { return "Success";                              }
            case MessageStatus::RUNNING:                    { return "Running";                              }
            case MessageStatus::TIMEDOUT:                   { return "Timed out";                            }
            case MessageStatus::FOE_UNEXPECTED_OPCODE:      { return "Unexpected FoE opcode";                }
            case MessageStatus::FOE_PACKET_NUMBER_WRONG:    { return "Unexpected FoE packet number";         }
            case MessageStatus::FOE_INVALID_REPLY:          { return "Invalid FoE reply";                    }
            default:                                        { return "Unknown FoE error";                    }
        }
    }
}
