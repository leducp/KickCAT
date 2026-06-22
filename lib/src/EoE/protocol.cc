#include <cinttypes>
#include <cstdio>

#include "EoE/protocol.h"

namespace kickcat::EoE
{
    std::string toString(Header const* header)
    {
        char buffer[256];
        std::snprintf(buffer, sizeof(buffer),
            "EoE type %u port %u last_fragment %u frame %u fragment %u offset/size %u (%u bytes)",
            header->type, header->port, header->last_fragment, header->frame_number,
            header->fragment_number, header->offset, bytesFromBlocks32(header->offset));
        return std::string(buffer);
    }

    namespace result
    {
        char const* toString(uint16_t result)
        {
            switch (result)
            {
                case SUCCESS:                { return "Success";                 }
                case UNSPECIFIED_ERROR:      { return "Unspecified error";       }
                case UNSUPPORTED_FRAME_TYPE: { return "Unsupported frame type";  }
                case NO_IP_SUPPORT:          { return "No IP support";           }
                case DHCP_NOT_SUPPORTED:     { return "DHCP not supported";      }
                case NO_FILTER_SUPPORT:      { return "No filter support";       }
                default:                     { return "Unknown";                 }
            }
        }
    }
}
