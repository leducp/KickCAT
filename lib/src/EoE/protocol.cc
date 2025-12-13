#include "EoE/protocol.h"

namespace kickcat::EoE
{
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
