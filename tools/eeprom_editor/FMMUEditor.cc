#include <array>
#include <imgui.h>

#include "Editors.h"

namespace kickcat::eeprom_editor::fmmu
{
    constexpr std::array FMMU_TYPES =
    {
        "Not used (0)",
        "Outputs (1)",
        "Inputs (2)",
        "SyncM Status (3)",
    };

    bool render(eeprom::SII& sii)
    {
        bool changed = false;
        auto& fmmus = sii.fmmus;

        ImGui::TextColored(ImVec4(0.42f, 0.55f, 0.84f, 1.0f), "FMMU -- Category 40");
        ImGui::Separator();

        constexpr ImGuiTableFlags table_flags =
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit;

        if (ImGui::BeginTable("##fmmu_table", 3, table_flags))
        {
            ImGui::TableSetupColumn("#",        ImGuiTableColumnFlags_WidthFixed, 40.0f);
            ImGui::TableSetupColumn("Type",     ImGuiTableColumnFlags_WidthFixed, 180.0f);
            ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, 40.0f);
            ImGui::TableHeadersRow();

            int delete_idx = -1;

            for (std::size_t i = 0; i < fmmus.size(); ++i)
            {
                ImGui::PushID(static_cast<int>(i));
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%zu", i);

                ImGui::TableSetColumnIndex(1);
                {
                    int type_val = fmmus[i];
                    if (type_val >= static_cast<int>(FMMU_TYPES.size()))
                    {
                        type_val = 0;
                    }
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::Combo("##type", &type_val, FMMU_TYPES.data(), static_cast<int>(FMMU_TYPES.size())))
                    {
                        fmmus[i] = static_cast<uint8_t>(type_val);
                        changed = true;
                    }
                }

                ImGui::TableSetColumnIndex(2);
                if (ImGui::SmallButton("Del"))
                {
                    delete_idx = static_cast<int>(i);
                }

                ImGui::PopID();
            }

            ImGui::EndTable();

            if (delete_idx >= 0)
            {
                fmmus.erase(fmmus.begin() + delete_idx);
                changed = true;
            }
        }

        ImGui::Spacing();
        if (ImGui::Button("Add FMMU"))
        {
            fmmus.push_back(0);
            changed = true;
        }

        return changed;
    }
}
