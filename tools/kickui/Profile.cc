#include "Profile.h"

#include <iterator>

namespace kickcat::kickui
{
    namespace
    {
        // Single source of truth: register a profile by adding one line here (plus a
        // discovery case below, and -- if it has dedicated UI -- a Panel that
        // appliesTo it). is_drive contributes the motor control + MotorPanel.
        constexpr ProfileInfo REGISTRY[] = {
            {Profile::Generic,   "generic",       false},
            {Profile::CiA_DS402, "DS402 (drive)", true },
        };
        constexpr ProfileInfo UNKNOWN{Profile::Unknown, "unknown", false};
    }

    ProfileInfo const& profileInfo(Profile profile)
    {
        for (auto const& info : REGISTRY)
        {
            if (info.profile == profile) { return info; }
        }
        return UNKNOWN;
    }

    std::vector<ProfileInfo> const& selectableProfiles()
    {
        static std::vector<ProfileInfo> const list(std::begin(REGISTRY), std::end(REGISTRY));
        return list;
    }

    Profile profileFromCoeDeviceType(uint16_t device_type_low)
    {
        switch (device_type_low)
        {
            case 402: { return Profile::CiA_DS402; }
        }
        return Profile::Generic;
    }
}
