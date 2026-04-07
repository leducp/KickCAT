#include <cstdio>

#include "Editors.h"

namespace kickcat::eeprom_editor::pdo
{
    bool render(eeprom::SII& sii, Direction direction)
    {
        bool changed = false;

        bool const is_tx = (direction == Direction::Tx);

        char const* title = "RxPDO (Outputs) -- Category 51";
        auto* mappings_ptr = &sii.RxPDO;
        if (is_tx)
        {
            title        = "TxPDO (Inputs) -- Category 50";
            mappings_ptr = &sii.TxPDO;
        }
        auto& mappings = *mappings_ptr;
        auto& strings  = sii.strings;

        ImGui::TextColored(COLOR_TITLE, "%s", title);
        ImGui::Separator();

        int delete_mapping = -1;

        for (std::size_t m = 0; m < mappings.size(); ++m)
        {
            ImGui::PushID(static_cast<int>(m));
            auto& mapping = mappings[m];

            char header_label[128];
            std::snprintf(header_label, sizeof(header_label), "PDO 0x%04X - %s##m%zu",
                          mapping.index, resolveString(strings, mapping.name_index), m);

            if (ImGui::CollapsingHeader(header_label, ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImGui::Indent(8.0f);

                ImGui::SetNextItemWidth(100.0f);
                changed |= hexFieldInput("Index##mapping", mapping.index,
                    [&](uint16_t v){ mapping.index = v; }, 4);

                ImGui::SetNextItemWidth(60.0f);
                changed |= ImGui::InputScalar("Sync Manager##mapping", ImGuiDataType_U8,
                                               &mapping.sync_manager);

                ImGui::SetNextItemWidth(60.0f);
                changed |= ImGui::InputScalar("Synchronization##mapping", ImGuiDataType_U8,
                                               &mapping.synchronization);

                changed |= stringIndexInput("Name##mapping", &mapping.name_index, strings);

                ImGui::SetNextItemWidth(100.0f);
                changed |= hexFieldInput("Flags##mapping", mapping.flags,
                    [&](uint16_t v){ mapping.flags = v; }, 4);

                ImGui::Spacing();

                constexpr ImGuiTableFlags table_flags =
                    ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg
                    | ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_Resizable;

                if (ImGui::BeginTable("##entries", 7, table_flags))
                {
                    ImGui::TableSetupColumn("Index",     ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    ImGui::TableSetupColumn("SubIndex",  ImGuiTableColumnFlags_WidthFixed, 70.0f);
                    ImGui::TableSetupColumn("Name",      ImGuiTableColumnFlags_WidthStretch);
                    ImGui::TableSetupColumn("DataType",  ImGuiTableColumnFlags_WidthFixed, 70.0f);
                    ImGui::TableSetupColumn("BitLen",    ImGuiTableColumnFlags_WidthFixed, 60.0f);
                    ImGui::TableSetupColumn("Flags",     ImGuiTableColumnFlags_WidthFixed, 80.0f);
                    ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, 40.0f);
                    ImGui::TableHeadersRow();

                    int delete_entry = -1;

                    for (std::size_t e = 0; e < mapping.entries.size(); ++e)
                    {
                        ImGui::PushID(static_cast<int>(e));
                        ImGui::TableNextRow();
                        auto& entry = mapping.entries[e];

                        ImGui::TableSetColumnIndex(0);
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        changed |= hexFieldInput("##idx", entry.index,
                            [&](uint16_t v){ entry.index = v; }, 4);

                        ImGui::TableSetColumnIndex(1);
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        changed |= fieldInput("##sub", entry.subindex,
                            [&](uint8_t v){ entry.subindex = v; }, ImGuiDataType_U8);

                        ImGui::TableSetColumnIndex(2);
                        changed |= stringIndexInput("##name", &entry.name, strings);

                        ImGui::TableSetColumnIndex(3);
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        changed |= fieldInput("##dt", entry.data_type,
                            [&](uint8_t v){ entry.data_type = v; }, ImGuiDataType_U8);

                        ImGui::TableSetColumnIndex(4);
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        changed |= fieldInput("##bits", entry.bitlen,
                            [&](uint8_t v){ entry.bitlen = v; }, ImGuiDataType_U8);

                        ImGui::TableSetColumnIndex(5);
                        ImGui::SetNextItemWidth(-FLT_MIN);
                        changed |= hexFieldInput("##fl", entry.flags,
                            [&](uint16_t v){ entry.flags = v; }, 4);

                        ImGui::TableSetColumnIndex(6);
                        if (ImGui::SmallButton("Del"))
                        {
                            delete_entry = static_cast<int>(e);
                        }

                        ImGui::PopID();
                    }

                    ImGui::EndTable();

                    if (delete_entry >= 0)
                    {
                        mapping.entries.erase(mapping.entries.begin() + delete_entry);
                        changed = true;
                    }
                }

                if (ImGui::SmallButton("Add Entry"))
                {
                    mapping.entries.push_back(eeprom::PDOEntry{});
                    changed = true;
                }

                ImGui::SameLine();
                if (ImGui::SmallButton("Remove Mapping"))
                {
                    delete_mapping = static_cast<int>(m);
                }

                ImGui::Unindent(8.0f);
            }

            ImGui::PopID();
        }

        if (delete_mapping >= 0)
        {
            mappings.erase(mappings.begin() + delete_mapping);
            changed = true;
        }

        ImGui::Spacing();
        if (ImGui::Button("Add PDO Mapping"))
        {
            mappings.push_back(eeprom::PDOMapping{});
            changed = true;
        }

        return changed;
    }
}
