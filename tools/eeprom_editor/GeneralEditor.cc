#include <array>

#include "Editors.h"

namespace kickcat::eeprom_editor::general
{
    constexpr std::array PORT_TYPES =
    {
        "Not implemented (0)",
        "Not configured (1)",
        "EBUS (2)",
        "MII (3)",
    };

    bool render(eeprom::SII& sii)
    {
        bool changed = false;
        auto& g = sii.general;

        ImGui::TextColored(ImVec4(0.42f, 0.55f, 0.84f, 1.0f), "General -- Category 30");
        ImGui::Separator();

        if (ImGui::CollapsingHeader("String References", ImGuiTreeNodeFlags_DefaultOpen))
        {
            changed |= stringIndexInput("Group Info##gen",   &g.group_info_id,   sii.strings);
            changed |= stringIndexInput("Image Name##gen",   &g.image_name_id,   sii.strings);
            changed |= stringIndexInput("Device Order##gen", &g.device_order_id, sii.strings);
            changed |= stringIndexInput("Device Name##gen",  &g.device_name_id,  sii.strings);
        }

        if (ImGui::CollapsingHeader("CoE Details", ImGuiTreeNodeFlags_DefaultOpen))
        {
            auto bitfield_check = [&](char const* label, bool value, auto setter) -> bool
            {
                bool on = value;
                if (ImGui::Checkbox(label, &on))
                {
                    int val = 0;
                    if (on)
                    {
                        val = 1;
                    }
                    setter(val);
                    return true;
                }
                return false;
            };

            changed |= bitfield_check("SDO Set",             g.SDO_set,             [&](int v){ g.SDO_set = v; });
            changed |= bitfield_check("SDO Info",            g.SDO_info,            [&](int v){ g.SDO_info = v; });
            changed |= bitfield_check("PDO Assign",          g.PDO_assign,          [&](int v){ g.PDO_assign = v; });
            changed |= bitfield_check("PDO Configuration",   g.PDO_configuration,   [&](int v){ g.PDO_configuration = v; });
            changed |= bitfield_check("PDO Upload",          g.PDO_upload,          [&](int v){ g.PDO_upload = v; });
            changed |= bitfield_check("SDO Complete Access",  g.SDO_complete_access, [&](int v){ g.SDO_complete_access = v; });
        }

        if (ImGui::CollapsingHeader("Protocol Details"))
        {
            ImGui::SetNextItemWidth(100.0f);
            changed |= fieldInput("FoE Details", g.FoE_details,
                [&](uint8_t v){ g.FoE_details = v; }, ImGuiDataType_U8);

            ImGui::SetNextItemWidth(100.0f);
            changed |= fieldInput("EoE Details", g.EoE_details,
                [&](uint8_t v){ g.EoE_details = v; }, ImGuiDataType_U8);

            ImGui::SetNextItemWidth(100.0f);
            changed |= fieldInput("SoE Channels", g.SoE_channels,
                [&](uint8_t v){ g.SoE_channels = v; }, ImGuiDataType_U8);

            ImGui::SetNextItemWidth(100.0f);
            changed |= fieldInput("DS402 Channels", g.DS402_channels,
                [&](uint8_t v){ g.DS402_channels = v; }, ImGuiDataType_U8);

            ImGui::SetNextItemWidth(100.0f);
            changed |= fieldInput("Sysman Class", g.SysmanClass,
                [&](uint8_t v){ g.SysmanClass = v; }, ImGuiDataType_U8);
        }

        if (ImGui::CollapsingHeader("Port Types", ImGuiTreeNodeFlags_DefaultOpen))
        {
            auto port_dropdown = [&](char const* label, int current_val, auto setter) -> bool
            {
                int val = current_val;
                if (val >= static_cast<int>(PORT_TYPES.size()))
                {
                    val = 0;
                }

                ImGui::SetNextItemWidth(180.0f);
                if (ImGui::Combo(label, &val, PORT_TYPES.data(), static_cast<int>(PORT_TYPES.size())))
                {
                    setter(val);
                    return true;
                }
                return false;
            };

            changed |= port_dropdown("Port 0##port", g.port_0, [&](int v){ g.port_0 = v; });
            changed |= port_dropdown("Port 1##port", g.port_1, [&](int v){ g.port_1 = v; });
            changed |= port_dropdown("Port 2##port", g.port_2, [&](int v){ g.port_2 = v; });
            changed |= port_dropdown("Port 3##port", g.port_3, [&](int v){ g.port_3 = v; });
        }

        if (ImGui::CollapsingHeader("Misc"))
        {
            ImGui::SetNextItemWidth(100.0f);
            changed |= fieldInput("E-Bus Current (mA)", g.current_on_ebus,
                [&](int16_t v){ g.current_on_ebus = v; }, ImGuiDataType_S16);
            tooltipMarker("Negative = feeding current into bus");

            ImGui::SetNextItemWidth(100.0f);
            changed |= fieldInput("Flags##gen", g.flags,
                [&](uint8_t v){ g.flags = v; },
                ImGuiDataType_U8, "0x%02X", ImGuiInputTextFlags_CharsHexadecimal);

            ImGui::SetNextItemWidth(100.0f);
            changed |= fieldInput("Phys. Memory Addr", g.physical_memory_address,
                [&](uint16_t v){ g.physical_memory_address = v; },
                ImGuiDataType_U16, "0x%04X", ImGuiInputTextFlags_CharsHexadecimal);
        }

        return changed;
    }
}
