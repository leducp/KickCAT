#ifndef KICKCAT_EOE_PROTOCOL_H
#define KICKCAT_EOE_PROTOCOL_H

#include "kickcat/protocol.h"

namespace kickcat::EoE
{
    struct Header        // ETG1000.6 chapter 5.7.1 EoE
    {
        uint8_t type : 4,
                port : 4;
        uint8_t last_fragment: 1,
                time_appended: 1,
                time_request:  1,
                reserved:      5;
        uint16_t fragment_number: 6,
                 offset:          6,
                 frame_number:    4;
    } __attribute__((__packed__));
    std::string toString(Header const* header); // Stringify the whole message, not just the header

    struct Parameter      // ETG1000.6 chapter 5.7.4 Set IP Parameter
    {
        uint32_t mac_address:     1,
                 ip_address:      1,
                 subnet_mask:     1,
                 default_gateway: 1,
                 dns_server_ip:   1,
                 dns_name:        1,
                 reserved:        26;
    } __attribute__((__packed__));

    // NOTE: for request SET_IP, all theses fields are mandatory
    using MAC = uint8_t[6];     // MAC address according to ISO/IEC 8802-3
    using IP  = uint8_t[4];     // IP address according to IETF RFC 791
    using SUBNET_MASK = IP;     // Subnet mask according to IETF RFC 791
    using DEFAULT_GATEWAY = IP; // Default gateway address according to IETF RFC 791
    using DNS_SERVER_IP = IP;   // IP address of DNS server according to IETF RFC 791
    using DNS_NAME = char[32];  // DNS name according to IETF RFC 791

    namespace request
    {
        constexpr uint8_t SET_IP = 0x02;
        constexpr uint8_t GET_IP = 0x06;
    }

    namespace response
    {
        constexpr uint8_t SET_IP = 0x03;
        constexpr uint8_t GET_IP = 0x07;
    }

    namespace result
    {
        constexpr uint16_t SUCCESS                = 0x0000;
        constexpr uint16_t UNSPECIFIED_ERROR      = 0x0001;
        constexpr uint16_t UNSUPPORTED_FRAME_TYPE = 0x0002;
        constexpr uint16_t NO_IP_SUPPORT          = 0x0201;
        constexpr uint16_t DHCP_NOT_SUPPORTED     = 0x0202;
        constexpr uint16_t NO_FILTER_SUPPORT      = 0x0401;

        char const* toString(uint16_t result);
    }
}

#endif
