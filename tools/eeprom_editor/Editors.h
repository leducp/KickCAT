#ifndef KICKCAT_EEPROM_EDITOR_EDITORS_H
#define KICKCAT_EEPROM_EDITOR_EDITORS_H

#include <cstdint>
#include <string>
#include <vector>

#include "imgui.h"

#include "kickcat/SIIParser.h"

namespace kickcat::eeprom_editor
{
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
