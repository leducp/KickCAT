#include <array>

#include "Editors.h"

namespace kickcat::eeprom_editor::syncm
{
    constexpr std::array SM_TYPES =
    {
        "Unused (0)",
        "MailboxOut (1)",
        "MailboxIn (2)",
        "Output (3)",
        "Input (4)",
    };

    bool render(eeprom::SII& sii)
    {
        bool changed = false;
        auto& sms = sii.syncManagers;

        ImGui::TextColored(COLOR_TITLE, "Sync Managers -- Category 41");
        ImGui::Separator();

        constexpr ImGuiTableFlags table_flags =
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable
            | ImGuiTableFlags_SizingFixedFit;

        if (ImGui::BeginTable("##sm_table", 8, table_flags))
        {
            ImGui::TableSetupColumn("#",           ImGuiTableColumnFlags_WidthFixed, 30.0f);
            ImGui::TableSetupColumn("Start Addr",  ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Length",       ImGuiTableColumnFlags_WidthFixed, 70.0f);
            ImGui::TableSetupColumn("Control",      ImGuiTableColumnFlags_WidthFixed, 65.0f);
            ImGui::TableSetupColumn("Status",       ImGuiTableColumnFlags_WidthFixed, 65.0f);
            ImGui::TableSetupColumn("Enable",       ImGuiTableColumnFlags_WidthFixed, 55.0f);
            ImGui::TableSetupColumn("Type",         ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("##actions",    ImGuiTableColumnFlags_WidthFixed, 40.0f);
            ImGui::TableHeadersRow();

            int delete_idx = -1;

            for (std::size_t i = 0; i < sms.size(); ++i)
            {
                ImGui::PushID(static_cast<int>(i));
                ImGui::TableNextRow();
                auto& sm = sms[i];

                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%zu", i);

                ImGui::TableSetColumnIndex(1);
                ImGui::SetNextItemWidth(-FLT_MIN);
                changed |= fieldInput("##addr", sm.start_address,
                    [&](uint16_t v){ sm.start_address = v; },
                    ImGuiDataType_U16, "0x%04X", ImGuiInputTextFlags_CharsHexadecimal);

                ImGui::TableSetColumnIndex(2);
                ImGui::SetNextItemWidth(-FLT_MIN);
                changed |= fieldInput("##len", sm.length,
                    [&](uint16_t v){ sm.length = v; }, ImGuiDataType_U16);

                ImGui::TableSetColumnIndex(3);
                ImGui::SetNextItemWidth(-FLT_MIN);
                changed |= fieldInput("##ctrl", sm.control_register,
                    [&](uint8_t v){ sm.control_register = v; },
                    ImGuiDataType_U8, "0x%02X", ImGuiInputTextFlags_CharsHexadecimal);

                ImGui::TableSetColumnIndex(4);
                ImGui::SetNextItemWidth(-FLT_MIN);
                changed |= fieldInput("##stat", sm.status_register,
                    [&](uint8_t v){ sm.status_register = v; },
                    ImGuiDataType_U8, "0x%02X", ImGuiInputTextFlags_CharsHexadecimal);

                ImGui::TableSetColumnIndex(5);
                ImGui::SetNextItemWidth(-FLT_MIN);
                changed |= fieldInput("##en", sm.enable,
                    [&](uint8_t v){ sm.enable = v; }, ImGuiDataType_U8);

                ImGui::TableSetColumnIndex(6);
                {
                    int type_val = sm.type;
                    if (type_val >= static_cast<int>(SM_TYPES.size()))
                    {
                        type_val = 0;
                    }
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::Combo("##type", &type_val, SM_TYPES.data(), static_cast<int>(SM_TYPES.size())))
                    {
                        sm.type = static_cast<uint8_t>(type_val);
                        changed = true;
                    }
                }

                ImGui::TableSetColumnIndex(7);
                if (ImGui::SmallButton("Del"))
                {
                    delete_idx = static_cast<int>(i);
                }

                ImGui::PopID();
            }

            ImGui::EndTable();

            if (delete_idx >= 0)
            {
                sms.erase(sms.begin() + delete_idx);
                changed = true;
            }
        }

        ImGui::Spacing();
        if (ImGui::Button("Add Sync Manager"))
        {
            sms.push_back(eeprom::SyncManagerEntry{});
            changed = true;
        }

        return changed;
    }
}
