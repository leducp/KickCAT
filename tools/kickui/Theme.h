#ifndef KICKCAT_TOOLS_KICKUI_THEME_H
#define KICKCAT_TOOLS_KICKUI_THEME_H

#include <array>
#include <cstdint>

#include "imgui.h"

#include "kickcat/protocol.h"

namespace kickcat::kickui
{
    // Fixed-width font for streaming numbers (set once from GuiApp::monoFont() in
    // main); null until then, callers fall back to the default font.
    extern ImFont* g_mono_font;

    // HiDPI UI scale (set from GuiApp::scale() in main). ImGui scales the font
    // and style but not hardcoded dimensions.
    extern float g_ui_scale;

    // Explicit pixel sizes (fixed table columns, item widths) go through this.
    float px(float v);

    extern ImVec4 const COLOR_RED;
    extern ImVec4 const COLOR_GREEN;
    extern ImVec4 const COLOR_YELLOW;
    extern ImVec4 const COLOR_DS402;

    // Per-ESM-state palette (shared by the topology borders and the legend).
    extern ImVec4 const COLOR_ESM_INIT;
    extern ImVec4 const COLOR_ESM_PREOP;
    extern ImVec4 const COLOR_ESM_SAFEOP;
    extern ImVec4 const COLOR_ESM_OP;
    extern ImVec4 const COLOR_ESM_OTHER;

    // AL status byte layout (ETG.1000): low nibble = state, bit 4 = error.
    constexpr uint8_t AL_STATE_MASK = State::MASK_STATE;  // 0x0F
    constexpr uint8_t AL_ERROR_BIT  = State::ERROR_ACK;   // 0x10

    char const* stateLabel(uint8_t al_status);

    // ESM colour from a raw AL status byte: the error bit wins, else by state.
    ImVec4 esmColor(uint8_t al_status);
    // Greyed to "unknown" when the bus has stopped responding.
    ImVec4 esmColor(uint8_t al_status, bool bus_lost);

    struct StateButton
    {
        char const* label;
        State       state;
    };
    // EtherCAT state buttons for the common strip and the topology context menu.
    extern std::array<StateButton, 4> const STATE_BUTTONS;

    // One wording (and one colour scheme) for the redundancy status, everywhere.
    constexpr char const* REDUNDANCY_ACTIVE_TEXT =
        "Cable redundancy: ACTIVE \xe2\x80\x94 a cable is broken (ring still closed)";
    constexpr char const* REDUNDANCY_OK_TEXT = "Cable redundancy: ring intact";
    void renderRedundancyStatus(bool active);
}

#endif
