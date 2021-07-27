#ifndef KICKCAT_PROTOCOL_H
#define KICKCAT_PROTOCOL_H

#include <cstdint>
#include <cstdio>
#include <string_view>

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
    constexpr int32_t MAX_ETHERCAT_DATAGRAMS = 15;       // max EtherCAT datagrams per Ethernet frame
    constexpr uint16_t MAX_ETHERCAT_PAYLOAD_SIZE = 1486; // max EtherCAT payload size possible

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
    constexpr char const* toString(State state)
    {
        uint8_t raw_state = state & 0xF;
        switch (raw_state)
        {
            case INVALID:     { return "invalid";     }
            case INIT:        { return "init";        }
            case PRE_OP:      { return "pre-op";      }
            case BOOT:        { return "boot";        }
            case SAFE_OP:     { return "safe-op";     }
            case OPERATIONAL: { return "operational"; }
            default:          { return "unknown";     }
        }
    }

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

        constexpr uint16_t AL_CONTROL     = 0x120;
        constexpr uint16_t AL_STATUS      = 0x130;
        constexpr uint16_t AL_STATUS_CODE = 0x134;

        constexpr uint16_t PDI_CONTROL   = 0x140;
        constexpr uint16_t ESC_CONFIG    = 0x141;

        constexpr uint16_t ECAT_EVENT_MASK = 0x200;
        constexpr uint16_t ERROR_COUNTERS  = 0x300;

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
    }

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
            AoE = 0x01,
            EoE = 0x02,
            CoE = 0x04,
            FoE = 0x08,
            SoE = 0x10
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

        struct ServiceData // CoE type
        {
            uint16_t number : 9,
                    reserved : 3,
                    service : 4; // i.e. request, response
            uint8_t size_indicator : 1,
                    transfer_type : 1, // expedited or not
                    block_size : 2,
                    complete_access : 1,
                    command : 3; // i.e. upload
            uint16_t index;
            uint8_t subindex;
        } __attribute__((__packed__));

        /// ETG 1000.6
        struct Emergency
        {
            uint16_t number : 9,
                     reserved : 3,
                     service : 4; // i.e. request, response

            uint16_t error_code;
            uint8_t  error_register;
            uint8_t  data[5];
        } __attribute__((__packed__));
    }

    namespace CoE
    {
        constexpr uint16_t SM_COM_TYPE       = 0x1C00; // each sub-entry described SM[x] com type (mailbox in/out, PDO in/out, not used)
        constexpr uint16_t SM_CHANNEL        = 0x1C10; // each entry is associated with the mapped PDOs (if in used)

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

            std::string_view abort_to_str(uint32_t abort_code);
        }
    }

    // MAC addresses are not used by EtherCAT but set them helps the debug easier when following a network trace.
    constexpr uint8_t PRIMARY_IF_MAC[6]   = { 0x02, 0x00, 0xCA, 0xCA, 0x00, 0xFF };
    constexpr uint8_t SECONDARY_IF_MAC[6] = { 0x02, 0x00, 0xCA, 0xFF, 0xEE, 0xFF };

    // helpers
    constexpr uint16_t datagram_size(uint16_t data_size)
    {
        return sizeof(DatagramHeader) + data_size + sizeof(uint16_t);
    }

    /// create a position or node address
    constexpr uint32_t createAddress(uint16_t position, uint16_t offset)
    {
        return ((offset << 16) | position);
    }
}

#endif
