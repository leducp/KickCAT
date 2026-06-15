#include "PdoValuesPanel.h"

#include <cstdint>
#include <cstdio>
#include <vector>

#include "imgui.h"

#include "BusSession.h"
#include "BusProtocol.h"
#include "Theme.h"

namespace kickcat::kickui
{
    namespace
    {
        void pdoBytes(char const* title, std::vector<uint8_t> const& data)
        {
            ImGui::SeparatorText(title);
            if (data.empty())
            {
                ImGui::TextDisabled("(no data)");
                return;
            }
            // Fixed-width font so digits don't jitter as values change at 1 kHz.
            bool mono = (g_mono_font != nullptr);
            if (mono) { ImGui::PushFont(g_mono_font); }
            // 16 bytes per row: offset, hex, ascii-ish.
            char line[128];
            for (size_t row = 0; row < data.size(); row += 16)
            {
                int n = std::snprintf(line, sizeof(line), "%04zu: ", row);
                for (size_t i = row; (i < row + 16) and (i < data.size()); ++i)
                {
                    n += std::snprintf(line + n, sizeof(line) - n, "%02X ", data[i]);
                }
                ImGui::TextUnformatted(line);
            }
            if (mono) { ImGui::PopFont(); }
        }
    }

    bool PdoValuesPanel::appliesTo(Device const& device) const
    {
        return device.has_coe;
    }

    void PdoValuesPanel::render(BusSession& session, Device& device)
    {
        if (not device.isOperating())
        {
            ImGui::TextDisabled("Include this slave and Apply mapping, then bring it to\n"
                                "SAFE-OP/OP to see its process data.");
            return;
        }
        // Process-image bytes come from the published snapshot, not the live
        // MotorControl, so the UI never reaches into the RT thread's state.
        auto snap = session.snapshot();
        if (not snap or (device.index < 0) or (device.index >= static_cast<int>(snap->slaves.size())))
        {
            ImGui::TextDisabled("(no data)");
            return;
        }
        SlaveSnapshot const& ss = snap->slaves[device.index];
        pdoBytes("Input  (TxPDO, slave \xe2\x86\x92 master)", ss.in_raw);
        pdoBytes("Output (RxPDO, master \xe2\x86\x92 slave)", ss.out_raw);
    }
}
