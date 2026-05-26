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

        CoE::Dictionary          dictionary;
    };
}

#endif
