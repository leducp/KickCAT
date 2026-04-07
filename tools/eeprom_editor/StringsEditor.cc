#include "Editors.h"

#include <misc/cpp/imgui_stdlib.h>

namespace kickcat::eeprom_editor::strings
{
    bool render(eeprom::SII& sii)
    {
        bool changed = false;
        auto& strings = sii.strings;

        ImGui::TextColored(COLOR_TITLE, "Strings -- Category 10");
        ImGui::Separator();

        if (strings.empty())
        {
            strings.emplace_back();
        }

        constexpr ImGuiTableFlags table_flags =
            ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable;

        if (ImGui::BeginTable("##strings_table", 3, table_flags))
        {
            ImGui::TableSetupColumn("Index", ImGuiTableColumnFlags_WidthFixed, 50.0f);
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableHeadersRow();

            int delete_idx = -1;

            for (std::size_t i = 0; i < strings.size(); ++i)
            {
                ImGui::PushID(static_cast<int>(i));
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::Text("%zu", i);

                ImGui::TableSetColumnIndex(1);
                if (i == 0)
                {
                    ImGui::TextDisabled("(reserved empty string)");
                }
                else
                {
                    ImGui::SetNextItemWidth(-FLT_MIN);
                    if (ImGui::InputText("##val", &strings[i]))
                    {
                        changed = true;
                    }
                }

                ImGui::TableSetColumnIndex(2);
                if (i == 0)
                {
                    ImGui::TextDisabled("--");
                }
                else
                {
                    if (ImGui::SmallButton("Del"))
                    {
                        delete_idx = static_cast<int>(i);
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
                    {
                        ImGui::SetTooltip("Deleting shifts subsequent indices.\n"
                                          "String references in General/PDO may break.");
                    }
                }

                ImGui::PopID();
            }

            ImGui::EndTable();

            if (delete_idx > 0)
            {
                strings.erase(strings.begin() + delete_idx);
                changed = true;
            }
        }

        ImGui::Spacing();
        if (ImGui::Button("Add String"))
        {
            strings.emplace_back("new string");
            changed = true;
        }

        return changed;
    }
}
