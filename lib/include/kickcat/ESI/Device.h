#ifndef KICKCAT_ESI_DEVICE_H
#define KICKCAT_ESI_DEVICE_H

#include <cstdint>
#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "kickcat/CoE/OD.h"

namespace kickcat::ESI
{
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
        std::string     type;
        uint32_t        product_code = 0;
        uint32_t        revision_no  = 0;
        uint32_t        serial_no    = 0;
        std::string     name;
        std::string     group_type;
        uint16_t        profile_no = 0;

        std::string     vendor_name;
        uint32_t        vendor_id = 0;

        CoE::Dictionary dictionary;
    };
}

#endif
