#ifndef KICKCAT_EEPROM_EDITOR_EDITORS_H
#define KICKCAT_EEPROM_EDITOR_EDITORS_H

#include <cstdint>
#include <string>
#include <vector>

#include "imgui.h"

#include "kickcat/SIIParser.h"

namespace kickcat::eeprom_editor
{
    constexpr ImVec4 COLOR_RED    {0.88f, 0.33f, 0.33f, 1.0f};
    constexpr ImVec4 COLOR_GREEN  {0.31f, 0.79f, 0.41f, 1.0f};
    constexpr ImVec4 COLOR_YELLOW {0.88f, 0.75f, 0.31f, 1.0f};
    constexpr ImVec4 COLOR_BLUE   {0.55f, 0.75f, 0.95f, 1.0f};
    constexpr ImVec4 COLOR_GREY   {0.65f, 0.65f, 0.65f, 1.0f};
    constexpr ImVec4 COLOR_DIM    {0.55f, 0.55f, 0.55f, 1.0f};
    constexpr ImVec4 COLOR_TITLE  {0.42f, 0.55f, 0.84f, 1.0f};

    char const* resolveString(std::vector<std::string> const& strings, uint8_t index);
    bool stringIndexInput(char const* label, uint8_t* index,
                          std::vector<std::string> const& strings);
    void tooltipMarker(char const* desc);

    template <typename T, typename Setter>
    bool fieldInput(char const* label, T value, Setter setter, ImGuiDataType data_type,
                    char const* fmt = nullptr, ImGuiInputTextFlags flags = ImGuiInputTextFlags_None)
    {
        if (ImGui::InputScalar(label, data_type, &value, nullptr, nullptr, fmt, flags))
        {
            setter(value);
            return true;
        }
        return false;
    }

    namespace info    { bool render(eeprom::SII& sii); }
    namespace strings { bool render(eeprom::SII& sii); }
    namespace general { bool render(eeprom::SII& sii); }
    namespace syncm   { bool render(eeprom::SII& sii); }
    namespace fmmu    { bool render(eeprom::SII& sii); }

    namespace pdo
    {
        enum class Direction { Tx, Rx };
        bool render(eeprom::SII& sii, Direction direction);
    }
}

#endif
