#ifndef KICKCAT_TOOLS_KICKUI_PROFILE_H
#define KICKCAT_TOOLS_KICKUI_PROFILE_H

#include <cstdint>
#include <vector>

namespace kickcat::kickui
{
    // A device's application profile. CoE/CiA profiles are discovered today; a
    // non-CoE profile (a SERCOS drive over SoE, a vendor profile) is added by
    // appending one enumerator, one registry entry, and one discovery mapping --
    // nothing in Device, the panels or the shell hardcodes a specific profile.
    // Standards-body-prefixed so non-CiA profiles (a SERCOS drive over SoE, a
    // vendor profile) read unambiguously in this flat enum: Sercos_*, Vendor_*, ...
    enum class Profile : uint16_t
    {
        Unknown,    // not discovered (no profile, or no mailbox that carries one)
        Generic,    // a device with a mailbox but no profile we specialise for
        CiA_DS402,  // CiA-402 drive
    };

    struct ProfileInfo
    {
        Profile     profile;
        char const* name;       // human label, e.g. "DS402 (drive)"
        bool        is_drive;   // contributes the motor/drive control + panel
    };

    // Metadata for a profile (a safe "unknown" entry for anything not registered).
    ProfileInfo const& profileInfo(Profile profile);

    // The profiles a user can force from the UI (everything but Unknown/"Auto").
    std::vector<ProfileInfo> const& selectableProfiles();

    // Discovery: map the CoE device-type (0x1000) low word to a Profile.
    Profile profileFromCoeDeviceType(uint16_t device_type_low);
}

#endif
