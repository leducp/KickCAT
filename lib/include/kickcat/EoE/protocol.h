#ifndef KICKCAT_EOE_PROTOCOL_H
#define KICKCAT_EOE_PROTOCOL_H

#include <array>
#include <cstddef>
#include <functional>
#include <vector>

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
        // Middle field is complete_size on the initiate fragment, offset on the rest (same bits).
        uint16_t fragment_number: 6,
                 offset:          6,
                 frame_number:    4;
    } __attribute__((__packed__));
    std::string toString(Header const* header); // Stringify the whole message, not just the header

    struct ParameterResponse  // ETG1000.6 Tables 84/87 (TEOEPARAHEADER): Set IP / Set filter response
    {
        uint8_t type : 4,
                port : 4;
        uint8_t last_fragment: 1,
                time_appended: 1,
                time_request:  1,
                reserved:      5;
        uint16_t result;
    } __attribute__((__packed__));

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

    struct AddressFilterHeader  // ETG1000.6 chapter 5.7.5 Set Address Filter (Table 86)
    {
        uint8_t mac_filter_count : 4,
                mac_filter_mask  : 2,
                reserved0        : 1,
                inhibit_broadcast: 1;
        uint8_t reserved1;
    } __attribute__((__packed__));

    // NOTE: for request SET_IP, all theses fields are mandatory
    using MAC = uint8_t[6];     // MAC address according to ISO/IEC 8802-3
    using IP  = uint8_t[4];     // IP address according to IETF RFC 791
    using SUBNET_MASK = IP;     // Subnet mask according to IETF RFC 791
    using DEFAULT_GATEWAY = IP; // Default gateway address according to IETF RFC 791
    using DNS_SERVER_IP = IP;   // IP address of DNS server according to IETF RFC 791
    using DNS_NAME = char[32];  // DNS name according to IETF RFC 791

    // Fragment sizes/offsets are expressed in 32-octet blocks (ETG.1000.6 Table 79/81).
    constexpr uint16_t blocks32(uint32_t bytes)
    {
        return static_cast<uint16_t>((bytes + 31) / 32);
    }
    constexpr uint32_t bytesFromBlocks32(uint16_t blocks)
    {
        return static_cast<uint32_t>(blocks) * 32;
    }

    namespace frame_type  // EoE Header 'type' field (ETG.1000.6 §5.7)
    {
        constexpr uint8_t FRAGMENT        = 0x00;  // frame fragment / initiate frame
        constexpr uint8_t SET_IP_REQ      = 0x02;
        constexpr uint8_t SET_IP_RESP     = 0x03;  // also Initiate EoE Response
        constexpr uint8_t SET_FILTER_REQ  = 0x04;
        constexpr uint8_t SET_FILTER_RESP = 0x05;
        constexpr uint8_t GET_IP_REQ      = 0x06;
        constexpr uint8_t GET_IP_RESP     = 0x07;
    }

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

    // Set/Get IP Parameter payload (ETG.1000.6 Table 83); *_included flags map onto EoE::Parameter.
    struct IpParameters
    {
        bool mac_included     = false;
        bool ip_included      = false;
        bool subnet_included  = false;
        bool gateway_included = false;
        bool dns_ip_included  = false;
        bool dns_name_included= false;

        MAC             mac{};
        IP              ip{};
        SUBNET_MASK     subnet{};
        DEFAULT_GATEWAY gateway{};
        DNS_SERVER_IP   dns_ip{};
        DNS_NAME        dns_name{};
    };

    // ETG.1000.6 Table 86. Field widths cap the counts at 15 addresses / 3 masks.
    struct AddressFilter
    {
        bool inhibit_broadcast = false;
        std::vector<std::array<uint8_t, 6>> addresses;
        std::vector<std::array<uint8_t, 6>> masks;
    };

    // The frame buffer is valid only for the duration of the call (the stack keeps no copy). The
    // sink may enqueue an outbound frame but must not drive the mailbox (receive/process) reentrantly.
    using FrameSink = std::function<void(uint8_t const* frame, size_t len, uint8_t port)>;

    // Application IP-stack endpoint on the slave side; each handler returns an EoE::result::* code.
    struct SlaveConfig
    {
        virtual ~SlaveConfig() = default;

        virtual uint16_t setIpParameter(IpParameters const& params) = 0;
        virtual uint16_t getIpParameter(IpParameters& params) = 0;
        virtual uint16_t setAddressFilter(uint8_t const* data, size_t len) = 0;
    };
}

#endif
