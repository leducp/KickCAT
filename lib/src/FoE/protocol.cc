#include "FoE/protocol.h"

namespace kickcat::FoE
{
    namespace result
    {
        char const* toString(uint16_t result)
        {
            switch (result)
            {
                case NOT_DEFINED:         { return "Not defined";         }
                case NOT_FOUND:           { return "Not found";           }
                case ACCESS_DENIED:       { return "Access denied";       }
                case DISK_FULL:           { return "Disk full";           }
                case ILLEGAL:             { return "Illegal";             }
                case PACKET_NUMBER_WRONG: { return "Packet number wrong"; }
                case ALREADY_EXISTS:      { return "Already exists";      }
                case NO_USER:             { return "No user";             }
                case BOOTSTRAP_ONLY:      { return "Bootstrap only";      }
                case NOT_BOOTSTRAP:       { return "Not bootstrap";       }
                case NO_RIGHTS:           { return "No rights";           }
                case PROGRAM_ERROR:       { return "Program error";       }
                default:                  { return "Unknown";            }
            }
        }
    }
}
