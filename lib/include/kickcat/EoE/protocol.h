#ifndef KICKCAT_EOE_PROTOCOL_H
#define KICKCAT_EOE_PROTOCOL_H

#include <cstddef>
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
        uint16_t fragment_number: 6,
                 offset:          6,
                 frame_number:    4;
    } __attribute__((__packed__));
    std::string toString(Header const* header); // Stringify the whole message, not just the header

    // ETG.1000.6 Figure 36/38 (TEOEPARAHEADER): Set IP / Set Address Filter responses carry the
    // 16-bit result in the EoE header's last word, in place of fragment_number/offset/frame_number.
    struct ParameterHeader
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

    using MAC = uint8_t[6];     // MAC address according to ISO/IEC 8802-3
    using IP  = uint8_t[4];     // IP address according to IETF RFC 791
    using SUBNET_MASK = IP;     // Subnet mask according to IETF RFC 791
    using DEFAULT_GATEWAY = IP; // Default gateway address according to IETF RFC 791
    using DNS_SERVER_IP = IP;   // IP address of DNS server according to IETF RFC 791
    using DNS_NAME = char[32];  // DNS name according to IETF RFC 791

    static_assert(sizeof(Header)          == 4, "EoE::Header must be 4 octets");
    static_assert(sizeof(ParameterHeader) == 4, "EoE::ParameterHeader must be 4 octets");
    static_assert(sizeof(Parameter)       == 4, "EoE::Parameter must be 4 octets");

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

    // ETG.1000.6: FrameType 0 carries a (fragment of a) tunneled Ethernet frame.
    constexpr uint8_t FRAME_FRAGMENT = 0x00;

    // The offset and complete-size header fields are expressed in 32-octet units.
    constexpr size_t FRAGMENT_GRANULARITY = 32;

    // The offset/complete-size fields are 6 bits (max 63), so a tunneled frame cannot exceed
    // 63 * 32 octets. Standard Ethernet frames (<= 1518 octets) fit comfortably.
    constexpr size_t MAX_FRAGMENTED_FRAME = 63 * FRAGMENT_GRANULARITY;

    // Largest Ethernet frame (with VLAN tag, without preamble/SFD/FCS) accepted by the Reassembler.
    constexpr size_t MAX_ETHERNET_FRAME = 1518;

    // Aggregate of the IP parameters exchanged by Set/Get IP. 'flags' tells which fields are valid.
    struct IpParameters
    {
        Parameter       flags{};
        MAC             mac{};
        IP              ip{};
        SUBNET_MASK     subnet_mask{};
        DEFAULT_GATEWAY default_gateway{};
        DNS_SERVER_IP   dns_server_ip{};
        DNS_NAME        dns_name{};
    };

    // Serialized size of the present IP parameters: flags word plus each enabled field.
    uint16_t ipParametersSize(IpParameters const& params);

    // Serialize the present IP parameters (flags word followed by each enabled field, in the
    // ETG.1000.6 Table 83 order) into dst. Returns the number of bytes written, or 0 if dst_capacity
    // is too small to hold them.
    uint16_t writeIpParameters(uint8_t* dst, size_t dst_capacity, IpParameters const& params);

    // Parse the flags word and the enabled fields from src (len bytes available). Returns the number
    // of bytes consumed, or 0 if the buffer is too short for the announced fields.
    uint16_t readIpParameters(uint8_t const* src, uint16_t len, IpParameters& params);

    // Human-readable conversions. macToString expects 6 octets, ipToString 4 octets (MAC/IP/etc).
    std::string macToString(uint8_t const* mac);
    std::string ipToString(uint8_t const* ip);
    std::string dnsName(IpParameters const& params); // bounded to 32 octets, NUL-safe
    std::string toString(IpParameters const& params); // summary of the present fields

    // Split a frame into EoE fragments sized to a mailbox. Header offset/size fields follow ETG.1000.6.
    class Fragmenter
    {
    public:
        Fragmenter(uint8_t const* frame, size_t size, uint8_t port, uint8_t frame_number, size_t max_data_per_fragment);

        bool done() const { return offset_ >= size_; }

        // Fill 'header' for the next fragment and point 'data' at its slice. Returns the slice length.
        // Shall not be called once done().
        size_t next(Header& header, uint8_t const*& data);

    private:
        uint8_t const* frame_;
        size_t   size_;
        uint8_t  port_;
        uint8_t  frame_number_;
        size_t   max_data_;        // rounded down to a multiple of FRAGMENT_GRANULARITY
        size_t   offset_{0};
        uint16_t fragment_no_{0};
    };

    // Accumulate fragments (in order) into a single frame.
    class Reassembler
    {
    public:
        // Add a fragment. Returns true when the frame is complete (last fragment seen); frame() is then valid.
        bool add(Header const* header, uint8_t const* data, size_t data_len);

        std::vector<uint8_t> const& frame() const { return buffer_; }
        void reset();

    private:
        std::vector<uint8_t> buffer_;
        uint16_t frame_number_{0};
        uint16_t next_fragment_{0};
        bool     in_progress_{false};
    };
}

#endif
