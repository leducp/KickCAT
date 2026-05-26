#ifndef KICKCAT_ESI_DEVICE_H
#define KICKCAT_ESI_DEVICE_H

#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "kickcat/CoE/OD.h"
#include "kickcat/protocol.h"

namespace kickcat::ESI
{
    struct SyncManager
    {
        kickcat::SyncManager::Type type = kickcat::SyncManager::Unused;
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

        std::vector<SyncManager> sync_managers;
        std::vector<SyncUnit>    sync_units;
        std::vector<Fmmu>        fmmus;
        Mailbox                  mailbox;

        CoE::Dictionary          dictionary;
    };
}

#endif
