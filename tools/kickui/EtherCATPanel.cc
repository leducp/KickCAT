#include "EtherCATPanel.h"

#include <vector>

#include "imgui.h"

#include "BusSession.h"
#include "Theme.h"

namespace kickcat::kickui
{
    namespace
    {
        void pdoTable(char const* title, std::vector<PdoEntry> const& entries)
        {
            ImGui::SeparatorText(title);
            if (entries.empty())
            {
                ImGui::TextDisabled("(none)");
                return;
            }
            if (not ImGui::BeginTable(title, 4,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
            {
                return;
            }
            ImGui::TableSetupColumn("PDO");
            ImGui::TableSetupColumn("Object");
            ImGui::TableSetupColumn("Sub");
            ImGui::TableSetupColumn("Bits");
            ImGui::TableHeadersRow();
            for (auto const& e : entries)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::Text("0x%04X", e.pdo);
                ImGui::TableNextColumn();
                ImGui::Text("0x%04X", e.index);
                ImGui::TableNextColumn();
                ImGui::Text("%u", e.sub);
                ImGui::TableNextColumn();
                ImGui::Text("%u", e.bits);
            }
            ImGui::EndTable();
        }

        // Editable entry table for one mapping object (0x16xx / 0x1Axx). Lets the
        // user add/remove entries (mapped object index/subindex/bit-length) and
        // pick the owning mapping object index.
        void entryEditor(char const* label, int* pdo_obj, std::vector<PdoEntry>& entries)
        {
            ImGui::PushID(label);
            ImGui::SeparatorText(label);
            ImGui::SetNextItemWidth(px(120.0f));
            ImGui::InputInt("mapping object (hex)", pdo_obj, 0, 0, ImGuiInputTextFlags_CharsHexadecimal);

            int total_bits = 0;
            for (auto const& e : entries) { total_bits += e.bits; }
            ImGui::SameLine();
            ImGui::TextDisabled("total: %d bits (%d bytes)", total_bits, total_bits / 8);

            int remove = -1;
            if (ImGui::BeginTable("entries", 4,
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
            {
                ImGui::TableSetupColumn("Object (hex)");
                ImGui::TableSetupColumn("Sub");
                ImGui::TableSetupColumn("Bits");
                ImGui::TableSetupColumn("");
                ImGui::TableHeadersRow();
                for (size_t i = 0; i < entries.size(); ++i)
                {
                    ImGui::TableNextRow();
                    ImGui::PushID(static_cast<int>(i));
                    PdoEntry& e = entries[i];
                    int idx = e.index, sub = e.sub, bits = e.bits;
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(px(90.0f));
                    if (ImGui::InputInt("##idx", &idx, 0, 0, ImGuiInputTextFlags_CharsHexadecimal))
                    {
                        e.index = static_cast<uint16_t>(idx);
                    }
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(px(50.0f));
                    if (ImGui::InputInt("##sub", &sub, 0, 0)) { e.sub = static_cast<uint8_t>(sub); }
                    ImGui::TableNextColumn();
                    ImGui::SetNextItemWidth(px(50.0f));
                    if (ImGui::InputInt("##bits", &bits, 0, 0)) { e.bits = static_cast<uint8_t>(bits); }
                    ImGui::TableNextColumn();
                    if (ImGui::SmallButton("X")) { remove = static_cast<int>(i); }
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            if (remove >= 0) { entries.erase(entries.begin() + remove); }
            if (ImGui::SmallButton("+ entry")) { entries.push_back(PdoEntry{}); }
            ImGui::PopID();
        }
    }

    bool EtherCATPanel::seedFromReadback(Device& device, bool set_objects)
    {
        PdoMapping m = device.pdoScan().mapping;
        if ((m.slave != device.index) or (not m.valid)) { return false; }
        edit_rx_ = m.rx;
        edit_tx_ = m.tx;
        if (set_objects)
        {
            if (not m.rx.empty()) { rx_obj_ = m.rx.front().pdo; }
            if (not m.tx.empty()) { tx_obj_ = m.tx.front().pdo; }
        }
        return true;
    }

    void EtherCATPanel::renderMappingEditor(Device& device)
    {
        // Pre-fill from the last read-back so the user edits the real layout; keep
        // trying until one actually arrives (the first frames have no read-back yet).
        if (not seeded_)
        {
            seeded_ = seedFromReadback(device, true);
        }
        ImGui::TextDisabled("Entries are written to the mapping object via SDO before\n"
                            "Apply mapping. \"Read PDO mapping\" first to start from the\n"
                            "slave's current layout.");
        if (ImGui::SmallButton("Load read-back"))
        {
            seedFromReadback(device, false);
        }
        entryEditor("RxPDO (outputs / 0x1C12)", &rx_obj_, edit_rx_);
        entryEditor("TxPDO (inputs / 0x1C13)", &tx_obj_, edit_tx_);
        if (ImGui::Button("Include with manual mapping"))
        {
            OperateConfig cfg;
            cfg.manual_mapping = true;
            cfg.rx_pdo_map     = static_cast<uint16_t>(rx_obj_);
            cfg.tx_pdo_map     = static_cast<uint16_t>(tx_obj_);
            cfg.manual_rx      = edit_rx_;
            cfg.manual_tx      = edit_tx_;
            device.includeSlave(cfg);
        }
    }

    bool EtherCATPanel::appliesTo(Device const& device) const
    {
        return device.has_coe;
    }

    void EtherCATPanel::render(BusSession& session, Device& device)
    {
        // Generic (non-motor) slaves can still be mapped via their SII PDO and
        // exchanged. DS402 motors are included from the Control tab instead.
        if (not device.isMotor())
        {
            ImGui::SeparatorText("Mapping");
            if (device.isConfigured())
            {
                ImGui::TextColored(COLOR_GREEN, "included \xe2\x80\x94 Apply mapping (sidebar) to operate");
                ImGui::SameLine();
                ImGui::BeginDisabled(session.isOperatingAny());
                if (ImGui::Button("Remove")) { device.unconfigureSlave(); }
                ImGui::EndDisabled();
            }
            else
            {
                ImGui::BeginDisabled(session.isOperatingAny());
                ImGui::Checkbox("Define PDO mapping manually", &manual_map_);
                if (manual_map_)
                {
                    renderMappingEditor(device);
                }
                else
                {
                    if (ImGui::Button("Include in mapping")) { device.includeSlave(); }
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("Map this slave (via its SII PDO) when Apply mapping runs;\n"
                                          "no DS402 control, just process-data exchange.");
                    }
                }
                ImGui::EndDisabled();
            }
        }

        ImGui::SeparatorText("PDO mapping (read-back)");
        PdoScan const scan = device.pdoScan();
        bool can_read = device.sdoAvailable() and not scan.running;
        ImGui::BeginDisabled(not can_read);
        if (ImGui::Button("Read PDO mapping"))
        {
            device.readPdoMapping();
        }
        ImGui::EndDisabled();
        if (scan.running)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("reading...");
        }

        PdoMapping const& mapping = scan.mapping;
        if (mapping.slave == device.index)
        {
            if (not mapping.error.empty())
            {
                ImGui::TextColored(COLOR_RED, "%s", mapping.error.c_str());
            }
            else if (mapping.valid)
            {
                pdoTable("RxPDO (outputs / 0x1C12)", mapping.rx);
                pdoTable("TxPDO (inputs / 0x1C13)", mapping.tx);
            }
        }
    }
}
