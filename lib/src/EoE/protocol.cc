#include <cstdio>
#include <cstring>
#include <string>

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

    namespace
    {
        char const* frameTypeToString(uint8_t type)
        {
            switch (type)
            {
                case FRAME_FRAGMENT:    { return "Frame fragment";    }
                case request::SET_IP:   { return "Set IP request";    }
                case response::SET_IP:  { return "Set IP response";   }
                case request::GET_IP:   { return "Get IP request";    }
                case response::GET_IP:  { return "Get IP response";   }
                default:                { return "Unknown";           }
            }
        }
    }

    std::string toString(Header const* header)
    {
        char buffer[128];
        std::snprintf(buffer, sizeof(buffer),
            "EoE %s (type %u) port %u last_fragment %u fragment %u offset %u frame %u",
            frameTypeToString(header->type), header->type, header->port,
            header->last_fragment, header->fragment_number, header->offset, header->frame_number);
        return std::string(buffer);
    }

    uint16_t ipParametersSize(IpParameters const& params)
    {
        uint16_t size = sizeof(Parameter);
        if (params.flags.mac_address)     { size += sizeof(MAC);             }
        if (params.flags.ip_address)      { size += sizeof(IP);              }
        if (params.flags.subnet_mask)     { size += sizeof(SUBNET_MASK);     }
        if (params.flags.default_gateway) { size += sizeof(DEFAULT_GATEWAY); }
        if (params.flags.dns_server_ip)   { size += sizeof(DNS_SERVER_IP);   }
        if (params.flags.dns_name)        { size += sizeof(DNS_NAME);        }
        return size;
    }

    uint16_t writeIpParameters(uint8_t* dst, size_t dst_capacity, IpParameters const& params)
    {
        if (dst_capacity < ipParametersSize(params))
        {
            return 0;
        }

        uint16_t offset = sizeof(Parameter);
        std::memcpy(dst, &params.flags, sizeof(Parameter));

        if (params.flags.mac_address)
        {
            std::memcpy(dst + offset, params.mac, sizeof(MAC));
            offset += sizeof(MAC);
        }
        if (params.flags.ip_address)
        {
            std::memcpy(dst + offset, params.ip, sizeof(IP));
            offset += sizeof(IP);
        }
        if (params.flags.subnet_mask)
        {
            std::memcpy(dst + offset, params.subnet_mask, sizeof(SUBNET_MASK));
            offset += sizeof(SUBNET_MASK);
        }
        if (params.flags.default_gateway)
        {
            std::memcpy(dst + offset, params.default_gateway, sizeof(DEFAULT_GATEWAY));
            offset += sizeof(DEFAULT_GATEWAY);
        }
        if (params.flags.dns_server_ip)
        {
            std::memcpy(dst + offset, params.dns_server_ip, sizeof(DNS_SERVER_IP));
            offset += sizeof(DNS_SERVER_IP);
        }
        if (params.flags.dns_name)
        {
            std::memcpy(dst + offset, params.dns_name, sizeof(DNS_NAME));
            offset += sizeof(DNS_NAME);
        }
        return offset;
    }

    uint16_t readIpParameters(uint8_t const* src, uint16_t len, IpParameters& params)
    {
        if (len < sizeof(Parameter))
        {
            return 0;
        }
        std::memcpy(&params.flags, src, sizeof(Parameter));
        uint16_t offset = sizeof(Parameter);

        auto take = [&](void* field, uint16_t size, bool present) -> bool
        {
            if (not present)
            {
                return true;
            }
            if ((offset + size) > len)
            {
                return false;
            }
            std::memcpy(field, src + offset, size);
            offset += size;
            return true;
        };

        if (not take(params.mac,             sizeof(MAC),             params.flags.mac_address))     { return 0; }
        if (not take(params.ip,              sizeof(IP),              params.flags.ip_address))      { return 0; }
        if (not take(params.subnet_mask,     sizeof(SUBNET_MASK),     params.flags.subnet_mask))     { return 0; }
        if (not take(params.default_gateway, sizeof(DEFAULT_GATEWAY), params.flags.default_gateway)) { return 0; }
        if (not take(params.dns_server_ip,   sizeof(DNS_SERVER_IP),   params.flags.dns_server_ip))   { return 0; }
        if (not take(params.dns_name,        sizeof(DNS_NAME),        params.flags.dns_name))        { return 0; }

        return offset;
    }

    std::string macToString(uint8_t const* mac)
    {
        char buffer[18];
        std::snprintf(buffer, sizeof(buffer), "%02x:%02x:%02x:%02x:%02x:%02x",
                      mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        return std::string(buffer);
    }

    std::string ipToString(uint8_t const* ip)
    {
        char buffer[16];
        std::snprintf(buffer, sizeof(buffer), "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);
        return std::string(buffer);
    }

    std::string dnsName(IpParameters const& params)
    {
        // the wire field is not guaranteed NUL-terminated: stop at the first NUL or 32 octets
        size_t length = 0;
        while ((length < sizeof(DNS_NAME)) and (params.dns_name[length] != '\0'))
        {
            length++;
        }
        return std::string(params.dns_name, length);
    }

    std::string toString(IpParameters const& params)
    {
        std::string out = "EoE IP parameters:";
        if (params.flags.mac_address)     { out += " mac=" + macToString(params.mac);                }
        if (params.flags.ip_address)      { out += " ip=" + ipToString(params.ip);                   }
        if (params.flags.subnet_mask)     { out += " subnet=" + ipToString(params.subnet_mask);      }
        if (params.flags.default_gateway) { out += " gateway=" + ipToString(params.default_gateway); }
        if (params.flags.dns_server_ip)   { out += " dns=" + ipToString(params.dns_server_ip);       }
        if (params.flags.dns_name)        { out += " dns_name=" + dnsName(params);                   }
        return out;
    }

    Fragmenter::Fragmenter(uint8_t const* frame, size_t size, uint8_t port, uint8_t frame_number, size_t max_data_per_fragment)
        : frame_{frame}
        , size_{size}
        , port_{port}
        , frame_number_{frame_number}
        , max_data_{(max_data_per_fragment / FRAGMENT_GRANULARITY) * FRAGMENT_GRANULARITY}
    {
        if (max_data_ == 0)
        {
            max_data_ = FRAGMENT_GRANULARITY;
        }
    }

    size_t Fragmenter::next(Header& header, uint8_t const*& data)
    {
        size_t remaining = size_ - offset_;
        size_t chunk = remaining;
        bool last = true;
        if (chunk > max_data_)
        {
            chunk = max_data_;
            last  = false;
        }

        header.type            = FRAME_FRAGMENT;
        header.port            = port_ & 0xf;
        header.last_fragment   = 0;
        if (last)
        {
            header.last_fragment = 1;
        }
        header.time_appended   = 0;
        header.time_request    = 0;
        header.reserved        = 0;
        header.fragment_number = fragment_no_ & 0x3f;
        header.frame_number    = frame_number_ & 0xf;
        if (fragment_no_ == 0)
        {
            // first fragment: offset field carries the complete frame size in 32-octet units (rounded up)
            header.offset = static_cast<uint16_t>(((size_ + FRAGMENT_GRANULARITY - 1) / FRAGMENT_GRANULARITY) & 0x3f);
        }
        else
        {
            header.offset = static_cast<uint16_t>((offset_ / FRAGMENT_GRANULARITY) & 0x3f);
        }

        data = frame_ + offset_;
        offset_ += chunk;
        fragment_no_++;
        return chunk;
    }

    bool Reassembler::add(Header const* header, uint8_t const* data, size_t data_len)
    {
        if (header->fragment_number == 0)
        {
            buffer_.clear();
            frame_number_  = header->frame_number;
            next_fragment_ = 0;
            in_progress_   = true;
        }
        else if (not in_progress_
              or header->frame_number    != frame_number_
              or header->fragment_number != next_fragment_)
        {
            // out-of-order or unrelated fragment: drop the partial frame
            in_progress_ = false;
            return false;
        }

        if ((buffer_.size() + data_len) > MAX_ETHERNET_FRAME)
        {
            // a frame larger than a legal Ethernet frame would grow buffer_ without bound: drop it
            in_progress_ = false;
            buffer_.clear();
            return false;
        }

        buffer_.insert(buffer_.end(), data, data + data_len);
        next_fragment_++;

        if (header->last_fragment)
        {
            in_progress_ = false;
            return true;
        }
        return false;
    }

    void Reassembler::reset()
    {
        buffer_.clear();
        next_fragment_ = 0;
        in_progress_   = false;
    }
}
