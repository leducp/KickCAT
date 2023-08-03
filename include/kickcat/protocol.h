#ifndef KICKCAT_PROTOCOL_H
#define KICKCAT_PROTOCOL_H

#include <cstdint>
#include <cstdio>
#include <string_view>
#include <string>
#include <array>
#include <tuple>

#include "Time.h"
#include "Error.h"

namespace kickcat
{
    // Host to Network byte order helper (Reminder: EtherCAT is LE, network is BE)
    template<typename T>
    constexpr T hton(T value)
    {
        if constexpr(sizeof(T) == 2)
        {
            return T((value << 8) | ((value >> 8) & 0xff));
        }
        else if constexpr(sizeof(T) == 4)
        {
            return T((value << 24) | ((value << 8) & 0x00ff0000) | ((value >> 8) & 0x0000ff00) | ((value >> 24) & 0xff));
        }
        else
        {
            THROW_ERROR("Size unsupported");
        }
    }

    // Ethernet description
    constexpr int32_t  MAC_SIZE = 6;
    using MAC = uint8_t[MAC_SIZE];

    struct EthernetHeader
    {
        MAC dst;
        MAC src;
        uint16_t type;
    } __attribute__((__packed__));

    constexpr int32_t  ETH_MTU_SIZE = 1500;
#ifndef ETH_FCS_SIZE
    constexpr int32_t  ETH_FCS_SIZE = 4;
#endif
    constexpr int32_t  ETH_MAX_SIZE = sizeof(EthernetHeader) + ETH_MTU_SIZE + ETH_FCS_SIZE;
    constexpr int32_t  ETH_MIN_SIZE = 60; // Ethernet disallow sending less than 64 bytes (60 + FCS)

    using EthernetFrame = std::array<uint8_t, ETH_MAX_SIZE>; // Definition of an Ethernet frame (maximal size)

    // EtherCAT description
    constexpr uint16_t ETH_ETHERCAT_TYPE = hton<uint16_t>(0x88A4);
    constexpr int32_t ETHERCAT_WKC_SIZE = 2;
    constexpr int32_t MAX_ETHERCAT_DATAGRAMS = 15;       // max EtherCAT datagrams per Ethernet frame
    constexpr uint16_t MAX_ETHERCAT_PAYLOAD_SIZE = 1486; // max EtherCAT payload size possible

    // EtherCAT subprotocol
    enum EthercatType : int16_t
    {
        ETHERCAT = 0x1,             // EtherCAT Data Link Protocol Data Unit (the real time protocol)
        NETWORK_VARIABLES = 0x4,
        MAILBOX  = 0x5              // EtherCAT gateway
    };

    struct EthercatHeader
    {
        uint16_t len : 11;
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
        ARMW = 13, // Auto increment physical Read Multiple Write - DC use
        FRMW = 14  // Configured address Physical Read Multiple Write - DC use
    };
    char const* toString(Command cmd);

    struct DatagramHeader
    {
        enum Command command;
        uint8_t index;
        uint32_t address;
        uint16_t len : 11,
                 reserved : 3,
                 circulating : 1,
                 multiple : 1; // multiple EtherCAT datagram (0 if last, 1 otherwise)
        uint16_t irq;
    } __attribute__((__packed__));
    std::string toString(DatagramHeader const& header);


    enum EcatEvent : uint16_t
    {
        DC_LATCH    = (1 << 0), // Clear: read Latch status
        // Reserved bit
        DL_STATUS   = (1 << 2), // Clear: read DL Status
        AL_STATUS   = (1 << 3), // Clear: read AL Status

        SM0_STATUS  = (1 << 4),
        SM1_STATUS  = (1 << 5),
        SM2_STATUS  = (1 << 6),
        SM3_STATUS  = (1 << 7),
        SM4_STATUS  = (1 << 8),
        SM5_STATUS  = (1 << 9),
        SM6_STATUS  = (1 << 10),
        SM7_STATUS  = (1 << 11),
    };


    // EtherCAT state machine states
    enum State : uint8_t
    {
        INVALID     = 0x00,
        INIT        = 0x01,
        PRE_OP      = 0x02,
        BOOT        = 0x03,
        SAFE_OP     = 0x04,
        OPERATIONAL = 0x08,
        ACK         = 0x10  // Acknowledge flag request - check AL_STATUS
    };
    char const* toString(State state);

    char const* ALStatus_to_string(int32_t code);

    //TODO need unit test on bitfield to check position !

    // EtherCAT standard registers
    namespace reg
    {
        constexpr uint16_t TYPE          = 0x0000;
        constexpr uint16_t REVISION      = 0x0001;
        constexpr uint16_t BUILD         = 0x0002; // 2 bytes
        constexpr uint16_t FMMU_SUP      = 0x0004;
        constexpr uint16_t SYNC_MNGR_SUP = 0x0005;
        constexpr uint16_t RAM_SIZE      = 0x0006;
        constexpr uint16_t PORT_DESC     = 0x0007;
        constexpr uint16_t ESC_FEATURES  = 0x0008; // 2 bytes

        constexpr uint16_t STATION_ADDR  = 0x0010; // 2 bytes
        constexpr uint16_t STATION_ALIAS = 0x0012; // 2 bytes

        constexpr uint16_t REG_WRITE_EN  = 0x0020;
        constexpr uint16_t REG_WRITE_PRO = 0x0021;

        constexpr uint16_t ESC_WRITE_EN  = 0x0030;
        constexpr uint16_t ESC_WRITE_PRO = 0x0031;

        constexpr uint16_t ESC_DL_FWRD   = 0x100;
        constexpr uint16_t ESC_DL_PORT   = 0x101;
        constexpr uint16_t ESC_DL_ALIAS  = 0x103;
        constexpr uint16_t ESC_DL_STATUS = 0x110;
        constexpr uint16_t ESC_DL_CONTROL = 0x100;

        constexpr uint16_t AL_CONTROL     = 0x120;
        constexpr uint16_t AL_STATUS      = 0x130;
        constexpr uint16_t AL_STATUS_CODE = 0x134;

        constexpr uint16_t PDI_CONTROL   = 0x140;
        constexpr uint16_t ESC_CONFIG    = 0x141;

        constexpr uint16_t ECAT_EVENT_MASK = 0x200;
        constexpr uint16_t ERROR_COUNTERS  = 0x300;

        constexpr uint16_t WDG_DIVIDER    = 0x400; // 2 bytes, Default 0x09C2 = 2498 = 100us
        constexpr uint16_t WDG_TIME_PDI   = 0x410; // 2 bytes, Default 0x03E8: 1000 * WDG_DIVIDER = 100ms
        constexpr uint16_t WDG_TIME_PDO   = 0x420; // 2 bytes, Default 0x03E8: 1000 * WDG_DIVIDER = 100ms

        constexpr uint16_t EEPROM_CONFIG  = 0x500;
        constexpr uint16_t EEPROM_PDI     = 0x501;
        constexpr uint16_t EEPROM_CONTROL = 0x502; // 2 bytes
        constexpr uint16_t EEPROM_ADDRESS = 0x504; // 4 bytes
        constexpr uint16_t EEPROM_DATA    = 0x508; // 8 bytes

        constexpr uint16_t FMMU            = 0x600; // each FFMU entry is described in 16 bytes (6x0 to 6xF), up to 16 FMMU

        constexpr uint16_t SYNC_MANAGER    = 0x800; // each SyncManager is described in 8 bytes, up to 8 Sync Manager
        constexpr uint16_t SYNC_MANAGER_0  = SYNC_MANAGER + 8 * 0;
        constexpr uint16_t SYNC_MANAGER_1  = SYNC_MANAGER + 8 * 1;
        constexpr uint16_t SYNC_MANAGER_2  = SYNC_MANAGER + 8 * 2;
        constexpr uint16_t SYNC_MANAGER_3  = SYNC_MANAGER + 8 * 3;
        constexpr uint16_t SM_STATS = 5;

        constexpr uint16_t DC_TIME            = 0x900;
        constexpr uint16_t DC_SYSTEM_TIME     = 0x910;
        constexpr uint16_t DC_SPEED_CNT_START = 0x930;
        constexpr uint16_t DC_TIME_FILTER     = 0x934;
        constexpr uint16_t DC_CYCLIC_CONTROL  = 0x980;
        constexpr uint16_t DC_SYNC_ACTIVATION = 0x981;

        constexpr uint16_t LATCH_STATUS = 0x9AE;
    }

    struct DLStatus
    {
        uint16_t  PDI_op : 1,
            PDI_watchdog : 1,
            EL_detection : 1,
            reserved : 1,
            PL_port0 : 1,
            PL_port1 : 1,
            PL_port2 : 1,
            PL_port3 : 1,
            LOOP_port0 : 1,
            COM_port0 : 1,
            LOOP_port1 : 1,
            COM_port1 : 1,
            LOOP_port2 : 1,
            COM_port2 : 1,
            LOOP_port3 : 1,
            COM_port3 : 1;
    }__attribute__((__packed__));

    std::string toString(DLStatus const& counters);

    struct ErrorCounters
    {
        struct RX
        {
            uint8_t invalid_frame;
            uint8_t physical_layer;
        };
        RX rx[4];
        uint8_t forwarded[4];
        uint8_t malformed_frame;
        uint8_t pdi;
        uint16_t spi_pdi;
        uint16_t uc_pdi;
        uint16_t avalon_pdi;
        uint16_t axi_pdi;
        uint8_t lost_link[4];
    } __attribute__((__packed__));

    std::string toString(ErrorCounters const& counters);

    struct SyncManager
    {
        uint16_t start_address;
        uint16_t length;
        uint8_t  control;
        uint8_t  status;
        uint8_t  activate;
        uint8_t  pdi_control;
    } __attribute__((__packed__));

    enum SyncManagerType
    {
        Unused     = 0,
        MailboxOut = 1,
        MailboxInt = 2,
        Output     = 3,
        Input      = 4  // slave to master
    };
    constexpr uint8_t MAILBOX_STATUS = (1 << 3);

    std::string toString(SyncManagerType const& type);

    struct FMMU
    {
        uint32_t logical_address;
        uint16_t length;
        uint8_t  logical_start_bit;
        uint8_t  logical_stop_bit;
        uint16_t physical_address;
        uint8_t  physical_start_bit;
        uint8_t  type;
        uint8_t  activate;
        uint8_t  reserved[3];
    } __attribute__((__packed__));

    namespace eeprom // addresses are in words!
    {
        constexpr uint16_t ESC_INFO            = 0x00;
        constexpr uint16_t ESC_PDI_CONTROL     = 0;
        constexpr uint16_t ESC_PDI_CONFIG      = 1;
        constexpr uint16_t ESC_SYNC_IMPULSE    = 2;
        constexpr uint16_t ESC_PDI_CONFIG_2    = 3;

        constexpr uint16_t ESC_STATION_ALIAS   = 0x04;
        constexpr uint16_t ESC_CRC             = 0x07;

        constexpr uint16_t VENDOR_ID           = 0x8;
        constexpr uint16_t PRODUCT_CODE        = 0xA;
        constexpr uint16_t REVISION_NUMBER     = 0xC;
        constexpr uint16_t SERIAL_NUMBER       = 0xE;

        constexpr uint16_t BOOTSTRAP_MAILBOX   = 0x14;
        constexpr uint16_t STANDARD_MAILBOX    = 0x18;
        constexpr uint16_t RECV_MBO_OFFSET     = 0;
        constexpr uint16_t RECV_MBO_SIZE       = 1;
        constexpr uint16_t SEND_MBO_OFFSET     = 2;
        constexpr uint16_t SEND_MBO_SIZE       = 3;
        constexpr uint16_t MAILBOX_PROTOCOL    = 0x1C;

        constexpr uint16_t EEPROM_SIZE         = 0x3E;
        constexpr uint16_t EEPROM_VERSION      = 0x3F;

        constexpr uint16_t START_CATEGORY      = 0x40;

        enum MailboxProtocol // get from EEPROM
        {
            None = 0x0,
            AoE  = 0x01,
            EoE  = 0x02,
            CoE  = 0x04,
            FoE  = 0x08,
            SoE  = 0x10
        };

        enum Command : uint16_t
        {
            NOP    = 0x0000,  // clear error bits
            READ   = 0x0100,
            WRITE  = 0x0201,
            RELOAD = 0x0300
        };

        enum Category : uint16_t
        {
            Strings   = 10,
            DataTypes = 20,
            General   = 30,
            FMMU      = 40,
            SyncM     = 41,
            TxPDO     = 50,
            RxPDO     = 51,
            DC        = 60,
            End       = 0xFFFF
        };

        struct GeneralEntry
        {
            uint8_t group_info_id;
            uint8_t image_name_id;
            uint8_t device_order_id;
            uint8_t device_name_id;
            uint8_t reserved_A;
            uint8_t SDO_set : 1, // CoE details
                    SDO_info : 1,
                    PDO_assign : 1,
                    PDO_configuration : 1,
                    PDO_upload : 1,
                    SDO_complete_access : 1,
                    unused : 2;
            uint8_t FoE_details;
            uint8_t EoE_details;
            uint8_t SoE_channels;
            uint8_t DS402_channels;
            uint8_t SysmanClass;
            uint8_t flags;
            int16_t current_on_ebus; // mA, negative means feeding current
            uint8_t group_info_id_dup;
            uint8_t reserved_B;
            uint16_t port_0 : 4,
                     port_1 : 4,
                     port_2 : 4,
                     port_3 : 4;
            uint16_t physical_memory_address;
            uint8_t reserved_C[12];
        } __attribute__((__packed__));

        struct SyncManagerEntry
        {
            uint16_t start_adress;
            uint16_t length;
            uint8_t  control_register;
            uint8_t  status_register;
            uint8_t  enable;
            uint8_t  type;
        } __attribute__((__packed__));

        struct PDOEntry
        {
            uint16_t index;
            uint8_t  subindex;
            uint8_t  name;
            uint8_t  data_type;
            uint8_t  bitlen;
            uint16_t flags;
        } __attribute__((__packed__));
    }

    std::string toString(eeprom::GeneralEntry const& general_entry);

    namespace mailbox
    {
        enum Type // to put in Mailbox header type entry
        {
            ERROR = 0x00,
            AoE   = 0x01,  // ADS over EtherCAT
            EoE   = 0x02,  // Ethernet over EthercAT
            CoE   = 0x03,  // CANopen over EtherCAT
            FoE   = 0x04,  // File over EtherCAT
            SoE   = 0x05,  // Servo over EtherCAT
            VoE   = 0x0F   // Vendor specific o er EtherCAT
        };

        struct Header
        {
            uint16_t len;
            uint16_t address;
            uint8_t  channel : 6,
                    priority : 2;
            uint8_t  type : 4, // type of the mailbox, i.e. CoE
                    count: 3,  // handle of the message
                    reserved : 1;
        } __attribute__((__packed__));

        /// ETG 8200

        // Bitmask to dissociate local message processing from gateway message processing
        constexpr uint16_t GATEWAY_MESSAGE_MASK = (1 << 15);

        // Maximum simultaneous pending request in the gateway
        // Shall be a power of two and it shall not override with GATEWAY_MESSAGE_MASK
        constexpr uint16_t GATEWAY_MAX_REQUEST = 1024;
    }

    namespace CoE
    {
        constexpr uint16_t SM_COM_TYPE       = 0x1C00; // each sub-entry described SM[x] com type (mailbox in/out, PDO in/out, not used)
        constexpr uint16_t SM_CHANNEL        = 0x1C10; // each entry is associated with the mapped PDOs (if in used)

        struct Header
        {
            uint16_t number : 9,
                    reserved : 3,
                    service : 4; // i.e. request, response
        } __attribute__((__packed__));

        struct ServiceData      // ETG1000.6 chapter 5.6.2 SDO
        {
            uint8_t size_indicator : 1,
                    transfer_type : 1, // expedited or not
                    block_size : 2,
                    complete_access : 1,
                    command : 3; // i.e. upload
            uint16_t index;
            uint8_t subindex;
        } __attribute__((__packed__));

        struct ServiceDataInfo // ETG1000.6 chapter 5.6.3 SDO Information
        {
            uint16_t opcode : 7,
                    incomplete : 1,
                    reserved : 8;
            uint16_t index;
        } __attribute__((__packed__));

        struct Emergency        // ETG1000.6 chapter 5.6.4 Emergency
        {
            uint16_t error_code;
            uint8_t  error_register;
            uint8_t  data[5];
        } __attribute__((__packed__));

        enum Service
        {
            EMERGENCY            = 0x01,
            SDO_REQUEST          = 0x02,
            SDO_RESPONSE         = 0x03,
            TxPDO                = 0x04,
            RxPDO                = 0x05,
            TxPDO_REMOTE_REQUEST = 0x06,
            RxPDO_REMOTE_REQUEST = 0x07,
            SDO_INFORMATION      = 0x08
        };

        namespace SDO
        {
            // Command specifiers depending on SDO request type
            namespace request
            {
                constexpr uint8_t DOWNLOAD_SEGMENTED = 0x00;
                constexpr uint8_t DOWNLOAD           = 0x01;
                constexpr uint8_t UPLOAD             = 0x02;
                constexpr uint8_t UPLOAD_SEGMENTED   = 0x03;
                constexpr uint8_t ABORT              = 0x04;
            }

            namespace response
            {
                constexpr uint8_t UPLOAD_SEGMENTED    = 0x00;
                constexpr uint8_t DOWNLOAD_SEGMENTED  = 0x01;
                constexpr uint8_t UPLOAD              = 0x02;
                constexpr uint8_t DOWNLOAD            = 0x03;
            }

            namespace information
            {
                constexpr uint8_t GET_OD_LIST_REQ    = 0x01;
                constexpr uint8_t GET_OD_LIST_RESP   = 0x02;
                constexpr uint8_t GET_OD_REQ         = 0x03;
                constexpr uint8_t GET_OD_RESP        = 0x04;
                constexpr uint8_t GET_ED_LIST_REQ    = 0x05;
                constexpr uint8_t GET_ED_LIST_RESP   = 0x06;
                constexpr uint8_t SDO_INFO_ERROR_REQ = 0x07;
            }

            char const* abort_to_str(uint32_t abort_code);
        }
    }

    // MAC addresses are not used by EtherCAT but set them helps the debug easier when following a network trace.
    constexpr MAC PRIMARY_IF_MAC   = { 0xCA, 0xDE, 0xCA, 0xDE, 0xDE, 0xFF };
    constexpr MAC SECONDARY_IF_MAC = { 0x03, 0x02, 0x02, 0x02, 0xFF, 0xFF };

    // helpers
    constexpr uint16_t datagram_size(uint16_t data_size)
    {
        return static_cast<uint16_t>(sizeof(DatagramHeader) + data_size + sizeof(uint16_t));
    }

    /// create a position or node address
    constexpr uint32_t createAddress(uint16_t position, uint16_t offset)
    {
        return ((offset << 16) | position);
    }

    /// extract a position or node address
    constexpr std::tuple<uint16_t, uint16_t> extractAddress(uint32_t address)
    {
        uint16_t offset   = address >> 16;
        uint16_t position = address & 0xFFFF;
        return std::make_tuple(position, offset);
    }

    /// compute the watchdog divider to set from the required watchdog increment step
    /// Note: the value to set is the divider minus 2
    constexpr uint16_t computeWatchdogDivider(nanoseconds precision = 100us)
    {
        // clock speed is 25MHz -> 1/25MHz = 40ns
        nanoseconds clock_period = 40ns;
        return static_cast<uint16_t>((precision / clock_period) - 2);
    }

    /// compute the watchdog time from the watchdog precision
    uint16_t computeWatchdogTime(nanoseconds watchdog, nanoseconds precision);

    // Helper to retrieve the header/payload in a frame.
    template<typename T, typename Previous>
    T* pointData(Previous* header_address)
    {
        return reinterpret_cast<T*>(header_address + 1);
    }
    template<> mailbox::Header* pointData<mailbox::Header, uint8_t>(uint8_t* base_address);
    template<> EthernetHeader*  pointData<EthernetHeader,  uint8_t>(uint8_t* base_address);

    template<typename T, typename Previous>
    T const* pointData(Previous const* header_address)
    {
        return reinterpret_cast<T const*>(header_address + 1);
    }
    template<> mailbox::Header const* pointData<mailbox::Header, uint8_t>(uint8_t const* base_address);
    template<> EthernetHeader const*  pointData<EthernetHeader,  uint8_t>(uint8_t const* base_address);
}

#endif
