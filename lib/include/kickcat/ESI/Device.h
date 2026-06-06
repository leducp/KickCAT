#ifndef KICKCAT_ESI_DEVICE_H
#define KICKCAT_ESI_DEVICE_H

#include <array>
#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "kickcat/CoE/OD.h"
#include "kickcat/protocol.h"

namespace kickcat::ESI
{
    // Distinct from kickcat::SyncManager (the runtime register-layout namespace,
    // which it would otherwise shadow): the parsed attributes of an ESI <Sm>.
    struct SmInfo
    {
        SyncManager::Type type = SyncManager::Unused;
        uint16_t min_size       = 0;
        uint16_t max_size       = 0;
        uint16_t default_size   = 0;
        uint16_t start_address  = 0;
        uint8_t  control_byte   = 0;
        uint8_t  enable         = 0;
        bool     is_virtual     = false;
        bool     op_only        = false;
    };

    struct SyncUnit
    {
        bool separate_su          = false;
        bool separate_frame       = false;
        bool frame_repeat_support = false;
    };

    struct Fmmu
    {
        fmmu::Type type = fmmu::Unused;
        // -1 when the attribute is absent. ESI Sm/Su attributes index into
        // the device's <Sm>/<Su> declaration order.
        int   sm      = -1;
        int   su      = -1;
        bool  op_only = false;
    };

    // ETG.2000 InitCmd Transition: ESM transitions during which the InitCmd applies.
    namespace transition
    {
        enum Type : uint8_t
        {
            IP = 0,  // Init   -> PreOp
            PS = 1,  // PreOp  -> SafeOp
            SO = 2,  // SafeOp -> Op
            SP = 3,  // SafeOp -> PreOp
            OP = 4,  // Op     -> PreOp
            OS = 5,  // Op     -> SafeOp
        };
        char const* toString(Type const& t);
        void fromString(std::string_view text, Type& out);
    }

    struct Mailbox
    {
        bool data_link_layer = false;
        bool real_time_mode  = false;

        struct CoE
        {
            struct InitCmd
            {
                std::vector<transition::Type> transitions;
                uint16_t    index    = 0;
                uint8_t     subindex = 0;
                std::vector<uint8_t> data;
                bool        adapt_automatically   = false;
                bool        complete_access       = false;
                bool        overwritten_by_module = false;
                std::string comment;
            };

            bool        sdo_info                   = false;
            bool        pdo_assign                 = false;
            bool        pdo_config                 = false;
            bool        pdo_upload                 = false;
            bool        complete_access            = false;
            bool        segmented_sdo              = false;
            bool        diag_history               = false;
            bool        sdo_upload_with_max_length = false;
            bool        time_distribution          = false;
            std::string eds_file;
            std::vector<InitCmd> init_cmds;
        };

        struct EoE
        {
            struct InitCmd
            {
                std::vector<transition::Type> transitions;
                int32_t     type = 0;
                std::vector<uint8_t> data;
                std::string comment;
            };

            bool ip         = false;
            bool mac        = false;
            bool time_stamp = false;
            std::vector<InitCmd> init_cmds;
        };

        struct FoE {};

        struct SoE
        {
            struct InitCmd
            {
                std::vector<transition::Type> transitions;
                int32_t     idn     = 0;
                int32_t     channel = 0;
                std::vector<uint8_t> data;
                std::string comment;
            };

            std::optional<int32_t> channel_count;
            bool                   drive_follows_bit3 = false;
            std::vector<InitCmd>   init_cmds;
        };

        struct AoE
        {
            struct InitCmd
            {
                std::vector<transition::Type> transitions;
                std::vector<uint8_t> data;
                std::string comment;
            };

            bool ads_router            = false;
            bool generate_own_net_id   = false;
            bool initialize_own_net_id = false;
            std::vector<InitCmd> init_cmds;
        };

        struct VoE {};

        std::optional<CoE> coe;
        std::optional<EoE> eoe;
        std::optional<FoE> foe;
        std::optional<SoE> soe;
        std::optional<AoE> aoe;
        std::optional<VoE> voe;
    };

    // ETG.2000 RxPdo/TxPdo: a Process Data Object declared at device level. The
    // master uses these to compose the 0x1C12/0x1C13 SyncManager assignment and
    // the 0x16xx/0x1Axx mapping CoE objects when the slave doesn't expose them
    // directly in <Objects>.
    struct PdoEntry
    {
        uint16_t    index    = 0;
        uint8_t     subindex = 0;
        uint16_t    bit_len  = 0;
        std::string name;
        std::string comment;
        std::string data_type;  // ESI text label (e.g. "UINT", "UDINT")

        bool                       fixed = false;
        std::optional<int32_t>     safety_conn_number;
        std::string                safety_pdo_entry_type;
    };

    struct Pdo
    {
        uint16_t              index = 0;
        std::string           name;
        std::optional<int32_t> sm;             // index into Device::sync_managers
        std::optional<int32_t> su;             // index into Device::sync_units
        bool                  fixed                 = false;
        bool                  mandatory             = false;
        bool                  is_virtual            = false;
        std::optional<int32_t> os_fac;
        std::optional<int32_t> os_min;
        std::optional<int32_t> os_max;
        std::optional<int32_t> os_index_inc;
        std::optional<int32_t> pdo_order;
        bool                  overwritten_by_module = false;
        bool                  sra_parameter         = false;
        std::string           safety_pdo_type;
        std::optional<int32_t> safety_conn_number;
        std::vector<uint16_t> exclude;         // mutually exclusive PDO indexes
        std::vector<int32_t>  excluded_sm;
        std::vector<PdoEntry> entries;
    };

    // ETG.2000 <Eeprom>: describes the slave's SII content. Two mutually
    // exclusive forms — raw_data populated (full image as hex) OR the
    // structured form (byte_size + config_data + optional bootstrap +
    // categories).
    struct Eeprom
    {
        struct Category
        {
            int32_t                    cat_no = 0;
            std::vector<uint8_t>       data;          // <Data>
            std::optional<std::string> data_string;   // <DataString>
            std::optional<int32_t>     data_uint;     // <DataUINT>
            std::optional<int32_t>     data_udint;    // <DataUDINT>
            bool                       preserve_online_data = false;
        };

        std::vector<uint8_t>     raw_data;        // <Data> at Eeprom level (raw image)
        std::optional<int32_t>   byte_size;
        std::vector<uint8_t>     config_data;     // first 16 SII bytes
        std::vector<uint8_t>     config_data2;
        std::vector<uint8_t>     bootstrap;       // bootstrap mailbox config
        std::vector<Category>    categories;
        bool                     assign_to_pdi = false;
    };

    // ETG.2000 <Dc>/<OpMode>: distributed-clocks configuration.
    struct OpMode
    {
        struct SyncTime
        {
            int32_t                value = 0;
            std::optional<int32_t> factor;
        };

        struct ShiftTime
        {
            int32_t                value = 0;
            std::optional<int32_t> factor;
            std::optional<bool>    input;
            std::optional<int32_t> output_delay_time;
            std::optional<int32_t> input_delay_time;
        };

        // <OpMode>/<Sm No="..">. The SyncType/CycleTime/ShiftTime children are
        // obsolete in the ESI XSD and have no fields here.
        struct SmConfig
        {
            struct PdoRef
            {
                uint16_t               index = 0;
                std::optional<int32_t> os_fac;   // <Pdo>/@OSFac: oversampling factor
            };

            int32_t             no = 0;   // required @No: target SyncManager index
            std::vector<PdoRef> pdos;
        };

        std::string             name;
        std::string             desc;
        uint32_t                assign_activate = 0;
        std::optional<uint32_t> activate_additional;

        std::array<std::optional<SyncTime>,  4> cycle_time;   // CycleTimeSync0..3
        std::array<std::optional<ShiftTime>, 4> shift_time;   // ShiftTimeSync0..3
        std::vector<SmConfig>                   sm_configs;
    };

    struct Dc
    {
        bool                unknown_frmw              = false;
        bool                unknown_64bit             = false;
        bool                external_ref_clock        = false;
        bool                potential_reference_clock = false;
        bool                time_loop_control_only    = false;
        bool                pdo_oversampling          = false;
        std::vector<OpMode> op_modes;
    };

    struct DeviceSummary
    {
        std::string type;
        uint32_t    product_code = 0;
        uint32_t    revision_no  = 0;
        uint32_t    serial_no    = 0;
        std::string name;
    };

    struct DeviceFilter
    {
        std::optional<std::string> type;
        std::optional<uint32_t>    product_code;
        std::optional<uint32_t>    revision_no;
        std::size_t                index = 0;
    };

    struct Device
    {
        std::string  type;
        uint32_t     product_code = 0;
        uint32_t     revision_no  = 0;
        uint32_t     serial_no    = 0;
        std::string  name;
        std::string  group_type;
        uint16_t     profile_no = 0;

        std::string  vendor_name;
        uint32_t     vendor_id = 0;

        std::vector<SmInfo>      sync_managers;
        std::vector<SyncUnit>    sync_units;
        std::vector<Fmmu>        fmmus;
        std::optional<Mailbox>   mailbox;
        std::vector<Pdo>         rx_pdos;
        std::vector<Pdo>         tx_pdos;
        std::optional<Eeprom>    eeprom;
        std::optional<Dc>        dc;

        CoE::Dictionary          dictionary;
    };
}

#endif
