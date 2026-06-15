#include "Theme.h"

namespace kickcat::kickui
{
    ImFont* g_mono_font = nullptr;
    float   g_ui_scale  = 1.0f;

    float px(float v)
    {
        return v * g_ui_scale;
    }

    ImVec4 const COLOR_RED{0.95f, 0.36f, 0.36f, 1.0f};
    ImVec4 const COLOR_GREEN{0.26f, 0.82f, 0.48f, 1.0f};
    ImVec4 const COLOR_YELLOW{0.95f, 0.78f, 0.30f, 1.0f};
    ImVec4 const COLOR_DS402{0.29f, 0.62f, 1.0f, 1.0f};

    ImVec4 const COLOR_ESM_INIT{0.70f, 0.48f, 0.95f, 1.0f};    // purple
    ImVec4 const COLOR_ESM_PREOP{0.36f, 0.60f, 1.00f, 1.0f};   // blue
    ImVec4 const COLOR_ESM_SAFEOP{0.95f, 0.58f, 0.25f, 1.0f};  // orange
    ImVec4 const COLOR_ESM_OP{0.26f, 0.82f, 0.48f, 1.0f};      // green
    ImVec4 const COLOR_ESM_OTHER{0.60f, 0.60f, 0.64f, 1.0f};   // boot/invalid

    char const* stateLabel(uint8_t al_status)
    {
        switch (al_status & AL_STATE_MASK)
        {
            case static_cast<uint8_t>(State::INIT):        { return "INIT";    }
            case static_cast<uint8_t>(State::PRE_OP):      { return "PRE-OP";  }
            case static_cast<uint8_t>(State::BOOT):        { return "BOOT";    }
            case static_cast<uint8_t>(State::SAFE_OP):     { return "SAFE-OP"; }
            case static_cast<uint8_t>(State::OPERATIONAL): { return "OP";      }
        }
        return "?";
    }

    ImVec4 esmColor(uint8_t al_status)
    {
        if (al_status & AL_ERROR_BIT)
        {
            return COLOR_RED;
        }
        State st = static_cast<State>(al_status & AL_STATE_MASK);
        if (st == State::OPERATIONAL) { return COLOR_ESM_OP; }
        if (st == State::SAFE_OP)     { return COLOR_ESM_SAFEOP; }
        if (st == State::PRE_OP)      { return COLOR_ESM_PREOP; }
        if (st == State::INIT)        { return COLOR_ESM_INIT; }
        return COLOR_ESM_OTHER;
    }

    ImVec4 esmColor(uint8_t al_status, bool bus_lost)
    {
        if (bus_lost) { return COLOR_ESM_OTHER; }
        return esmColor(al_status);
    }

    std::array<StateButton, 4> const STATE_BUTTONS = {{
        {"INIT",    State::INIT},
        {"PRE-OP",  State::PRE_OP},
        {"SAFE-OP", State::SAFE_OP},
        {"OP",      State::OPERATIONAL},
    }};

    void renderRedundancyStatus(bool active)
    {
        if (active)
        {
            ImGui::TextColored(COLOR_YELLOW, "%s", REDUNDANCY_ACTIVE_TEXT);
        }
        else
        {
            ImGui::TextColored(COLOR_GREEN, "%s", REDUNDANCY_OK_TEXT);
        }
    }
}
