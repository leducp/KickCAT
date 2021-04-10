#ifndef KICKCAT_PROTOCOL_H
#define KICKCATPROTOCOL_H

#include <cstdint>

namespace kickcat
{
    constexpr int32_t  ETH_HEADER_SIZE = 14;
    constexpr int32_t  ETH_MTU_SIZE = 1500;
    constexpr int32_t  ETH_FCS_SIZE = 4;
    constexpr int32_t  ETH_MAX_SIZE = ETH_HEADER_SIZE + ETH_MTU_SIZE + ETH_FCS_SIZE;
    constexpr uint16_t ETH_ETHERCAT_TYPE = 0x88A4;

    // MAC addresses are not used by EtherCAT but set them helps the debug easier when following a network trace.
    constexpr uint8_t PRIMARY_IF_MAC[6]   = { 0x02, 0xCA, 0xCA, 0x0F, 0xFF, 0xFF };
    constexpr uint8_t SECONDARY_IF_MAC[6] = { 0x02, 0xC0, 0xFF, 0xEE, 0x00, 0xFF };

    constexpr int32_t MAX_ETHERCAT_DATAGRAMS = 15; // max EtherCAT datagrams per Ethernet frame

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
        uint8_t address[4];
        uint16_t len : 11,
                 reserved : 3,
                 circulating : 1,
                 multiple : 1; // multiple EtherCAT datagram (0 if last, 1 otherwise)
        uint16_t IRQ;
    } __attribute__((__packed__));

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

    constexpr uint16_t SYNC_MANAGER_0 = 0x800;
    constexpr uint16_t SYNC_MANAGER_1 = SYNC_MANAGER_0 + 0x8;
    constexpr uint16_t SYNC_MANAGER_2 = SYNC_MANAGER_1 + 0x8;
    constexpr uint16_t SYNC_MANAGER_3 = SYNC_MANAGER_2 + 0x8;
}

#endif