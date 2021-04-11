#ifndef KICKCAT_PROTOCOL_H
#define KICKCATPROTOCOL_H

#include <cstdint>
#include <cstdio>
namespace kickcat
{
    // Ethernet description
    struct EthernetHeader
    {
        uint8_t dst_mac[6];
        uint8_t src_mac[6];
        uint16_t type;
    } __attribute__((__packed__));

    constexpr int32_t  ETH_MTU_SIZE = 1500;
    constexpr int32_t  ETH_FCS_SIZE = 4;
    constexpr int32_t  ETH_MAX_SIZE = sizeof(EthernetHeader) + ETH_MTU_SIZE + ETH_FCS_SIZE;
    constexpr int32_t  ETH_MIN_SIZE = 60; // Ethernet disallow sending less than 64 bytes (60 + FCS)

    // EtherCAT description
    constexpr uint16_t ETH_ETHERCAT_TYPE = __builtin_bswap16(0x88A4); //TODO: implement constexpr htons
    constexpr int32_t ETHERCAT_WKC_SIZE = 2;
    constexpr int32_t MAX_ETHERCAT_DATAGRAMS = 15; // max EtherCAT datagrams per Ethernet frame

    struct EthercatHeader
    {
        int16_t len : 11;
        int16_t reserved : 1;
        int16_t type : 4;
    } __attribute__((__packed__));

    // Ethercat command types
    enum class Command : uint8_t
    {
        NOP  = 0,  // No Operation
        APRD = 1,  // Auto increment Physical Read
        APWR = 2,  // Auto increment Physical Write
        APRW = 3,  // Auto increment Physical Read Write
        FPRD = 4,  // Configured address Physical Read
        FPWR = 5,  // Configured address Physical Write
        FPRW = 6,  // Configured address Physical Read Write
        BRD  = 7,  // Broadcast Read
        BWR  = 8,  // Broadcast Write
        BRW  = 9,  // Broadcast Read Write
        LRD  = 10, // Logical memory Read
        LWR  = 11, // Logical memory Write
        LRW  = 12, // Logical memory Read Write
        ARMW = 13, // Auto increment physical Read Multiple Write
        FRMW = 14  // Configured address Physical Read Multiple Write
    };

    struct DatagramHeader
    {
        enum Command command;
        uint8_t index;
        uint32_t address;
        uint16_t len : 11,
                 reserved : 3,
                 circulating : 1,
                 multiple : 1; // multiple EtherCAT datagram (0 if last, 1 otherwise)
        uint16_t IRQ;
    } __attribute__((__packed__));


    // EtherCAT state machine states
    enum class SM_STATE : int16_t
    {
        INVALID     = 0x00,
        INIT        = 0x01,
        PRE_OP      = 0x02,
        BOOT        = 0x03,
        SAFE_OP     = 0x04,
        OPERATIONAL = 0x08,
        ERROR       = 0x10
    };


    enum class MailboxType
    {
        AoE = 0x01,
        EoE = 0x02,
        CoE = 0x03,
        FoE = 0x08,
        SoE = 0x10
    };

    struct MailboxHeader
    {
        uint16_t len;
        uint16_t address;
        uint8_t  channel : 6,
                 priority : 2;
        uint8_t  type : 4, // type of the mailbox, i.e. CoE
                 count: 3,
                 reserved : 1;
    } __attribute__((__packed__));

    struct MailboxServiceData // CoE type -> SDO
    {
        uint16_t number : 9,
                 reserved : 3,
                 service : 4; // i.e. request, response
        uint8_t size_indicator : 1,
                transfer_type : 1,
                block_size : 2,
                complete_access : 1,
                command : 3; // i.e. upload
        uint16_t index;
        uint8_t subindex;
        uint32_t size;
    } __attribute__((__packed__));


    //TODO need unit test on bitfield to check position !

    // EtherCAT standard registers
    namespace registers
    {
        constexpr uint16_t ESC_DL_FWRD  = 0x100;
        constexpr uint16_t ESC_DL_PORT  = 0x101;
        constexpr uint16_t ESC_DL_ALIAS = 0x103;

        constexpr uint16_t SYNC_MANAGER_0 = 0x800;
        constexpr uint16_t SYNC_MANAGER_1 = SYNC_MANAGER_0 + 0x8;
        constexpr uint16_t SYNC_MANAGER_2 = SYNC_MANAGER_1 + 0x8;
        constexpr uint16_t SYNC_MANAGER_3 = SYNC_MANAGER_2 + 0x8;
    }

    // MAC addresses are not used by EtherCAT but set them helps the debug easier when following a network trace.
    constexpr uint8_t PRIMARY_IF_MAC[6]   = { 0x02, 0x00, 0xCA, 0xCA, 0x00, 0xFF };
    constexpr uint8_t SECONDARY_IF_MAC[6] = { 0x02, 0x00, 0xCA, 0xFF, 0xEE, 0xFF };

    // helpers

    /// extract the n-ieme byte of a contiguous data T
    template<typename T>
    constexpr uint8_t extractByte(int32_t n, T const& data)
    {
        int32_t bit_pos = (n - 1) * 8;
        return static_cast<uint8_t>((data >> bit_pos) & 0xFF);
    }


    /// create a position or node address
    constexpr uint32_t createAddress(uint16_t position, uint16_t offset)
    {
        return ((offset << 16) | position);
    }
}

#endif
