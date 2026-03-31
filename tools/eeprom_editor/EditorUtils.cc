#include "Editors.h"

namespace kickcat::eeprom_editor
{
    char const* resolveString(std::vector<std::string> const& strings, uint8_t index)
    {
        if (index >= strings.size())
        {
            return "(invalid)";
        }
        if (index == 0)
        {
            return "(empty)";
        }
        return strings[index].c_str();
    }

    bool stringIndexInput(char const* label, uint8_t* index,
                          std::vector<std::string> const& strings)
    {
        bool out_of_range = (*index >= strings.size()) and (*index != 0);

        if (out_of_range)
        {
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.88f, 0.33f, 0.33f, 1.0f));
        }

        ImGui::SetNextItemWidth(60.0f);
        bool changed = ImGui::InputScalar(label, ImGuiDataType_U8, index);

        if (out_of_range)
        {
            ImGui::PopStyleColor();
        }

        ImGui::SameLine();
        ImGui::TextDisabled("-> %s", resolveString(strings, *index));

        return changed;
    }

    void tooltipMarker(char const* desc)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        {
            ImGui::SetTooltip("%s", desc);
        }
    }
}
