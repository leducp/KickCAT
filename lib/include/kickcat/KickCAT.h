#ifndef KICKAT_H
#define KICKAT_H

namespace kickcat
{
    enum class DatagramState
    {
        LOST,
        SEND_ERROR,
        INVALID_WKC,
        NO_HANDLER,
        OK
    };

    constexpr char const* toString(DatagramState src)
    {
        switch (src)
        {
            case DatagramState::LOST:        { return "LOST";       }
            case DatagramState::SEND_ERROR:  { return "SEND_ERROR"; }
            case DatagramState::INVALID_WKC: { return "INVALID_WKC";}
            case DatagramState::NO_HANDLER:  { return "NO_HANDLER"; }
            case DatagramState::OK:          { return "OK";         }
            default:
            {
                return "Unknown";
            }
        }
    }
}

#endif
