#include "Editors.h"

namespace kickcat::eeprom_editor::info
{
    bool render(eeprom::SII& sii)
    {
        bool changed = false;
        auto& info = sii.info;

        ImGui::TextColored(COLOR_TITLE, "Info (Header) -- Words 0x00-0x3F");
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::CollapsingHeader("Config Data", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::SetNextItemWidth(100.0f);
            changed |= hexFieldInput("PDI Control", info.pdi_control,
                [&](uint16_t v){ info.pdi_control = v; }, 4);

            ImGui::SetNextItemWidth(100.0f);
            changed |= hexFieldInput("PDI Config", info.pdi_configuration,
                [&](uint16_t v){ info.pdi_configuration = v; }, 4);

            ImGui::SetNextItemWidth(100.0f);
            changed |= fieldInput("Sync Impulse", info.sync_impulse_length,
                [&](uint16_t v){ info.sync_impulse_length = v; }, ImGuiDataType_U16);

            ImGui::SetNextItemWidth(100.0f);
            changed |= hexFieldInput("PDI Config 2", info.pdi_configuration_2,
                [&](uint16_t v){ info.pdi_configuration_2 = v; }, 4);

            ImGui::SetNextItemWidth(100.0f);
            changed |= fieldInput("Station Alias", info.station_alias,
                [&](uint16_t v){ info.station_alias = v; }, ImGuiDataType_U16);

            uint8_t crc = static_cast<uint8_t>(info.crc);
            ImGui::SetNextItemWidth(100.0f);
            ImGui::InputScalar("CRC (auto)", ImGuiDataType_U8, &crc, nullptr, nullptr, "0x%02X",
                               ImGuiInputTextFlags_ReadOnly);
            tooltipMarker("CRC-8 over first 7 words, auto-computed on save");
        }

        if (ImGui::CollapsingHeader("Identity", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::SetNextItemWidth(140.0f);
            changed |= hexFieldInput("Vendor ID", info.vendor_id,
                [&](uint32_t v){ info.vendor_id = v; }, 8);

            ImGui::SetNextItemWidth(140.0f);
            changed |= hexFieldInput("Product Code", info.product_code,
                [&](uint32_t v){ info.product_code = v; }, 8);

            ImGui::SetNextItemWidth(140.0f);
            changed |= hexFieldInput("Revision", info.revision_number,
                [&](uint32_t v){ info.revision_number = v; }, 8);

            ImGui::SetNextItemWidth(140.0f);
            changed |= hexFieldInput("Serial", info.serial_number,
                [&](uint32_t v){ info.serial_number = v; }, 8);
        }

        if (ImGui::CollapsingHeader("Hardware Delays"))
        {
            ImGui::SetNextItemWidth(100.0f);
            changed |= fieldInput("Execution Delay", info.execution_delay,
                [&](uint16_t v){ info.execution_delay = v; }, ImGuiDataType_U16);

            ImGui::SetNextItemWidth(100.0f);
            changed |= fieldInput("Port 0 Delay", info.port0_delay,
                [&](uint16_t v){ info.port0_delay = v; }, ImGuiDataType_U16);

            ImGui::SetNextItemWidth(100.0f);
            changed |= fieldInput("Port 1 Delay", info.port1_delay,
                [&](uint16_t v){ info.port1_delay = v; }, ImGuiDataType_U16);
        }

        if (ImGui::CollapsingHeader("Bootstrap Mailbox"))
        {
            ImGui::SetNextItemWidth(100.0f);
            changed |= hexFieldInput("Recv Offset##boot", info.bootstrap_recv_mbx_offset,
                [&](uint16_t v){ info.bootstrap_recv_mbx_offset = v; }, 4);

            ImGui::SetNextItemWidth(100.0f);
            changed |= fieldInput("Recv Size##boot", info.bootstrap_recv_mbx_size,
                [&](uint16_t v){ info.bootstrap_recv_mbx_size = v; }, ImGuiDataType_U16);

            ImGui::SetNextItemWidth(100.0f);
            changed |= hexFieldInput("Send Offset##boot", info.bootstrap_send_mbx_offset,
                [&](uint16_t v){ info.bootstrap_send_mbx_offset = v; }, 4);

            ImGui::SetNextItemWidth(100.0f);
            changed |= fieldInput("Send Size##boot", info.bootstrap_send_mbx_size,
                [&](uint16_t v){ info.bootstrap_send_mbx_size = v; }, ImGuiDataType_U16);
        }

        if (ImGui::CollapsingHeader("Standard Mailbox"))
        {
            ImGui::SetNextItemWidth(100.0f);
            changed |= hexFieldInput("Recv Offset##std", info.standard_recv_mbx_offset,
                [&](uint16_t v){ info.standard_recv_mbx_offset = v; }, 4);

            ImGui::SetNextItemWidth(100.0f);
            changed |= fieldInput("Recv Size##std", info.standard_recv_mbx_size,
                [&](uint16_t v){ info.standard_recv_mbx_size = v; }, ImGuiDataType_U16);

            ImGui::SetNextItemWidth(100.0f);
            changed |= hexFieldInput("Send Offset##std", info.standard_send_mbx_offset,
                [&](uint16_t v){ info.standard_send_mbx_offset = v; }, 4);

            ImGui::SetNextItemWidth(100.0f);
            changed |= fieldInput("Send Size##std", info.standard_send_mbx_size,
                [&](uint16_t v){ info.standard_send_mbx_size = v; }, ImGuiDataType_U16);
        }

        if (ImGui::CollapsingHeader("Mailbox Protocols"))
        {
            uint16_t proto = info.mailbox_protocol;

            auto check = [&](char const* label, uint16_t bit) -> bool
            {
                bool on = (proto & bit) != 0;
                if (ImGui::Checkbox(label, &on))
                {
                    if (on)
                    {
                        proto |= bit;
                    }
                    else
                    {
                        proto &= ~bit;
                    }
                    info.mailbox_protocol = proto;
                    return true;
                }
                return false;
            };
            changed |= check("AoE", 0x01);
            changed |= check("EoE", 0x02);
            changed |= check("CoE", 0x04);
            changed |= check("FoE", 0x08);
            changed |= check("SoE", 0x10);
        }

        if (ImGui::CollapsingHeader("EEPROM"))
        {
            ImGui::SetNextItemWidth(100.0f);
            changed |= fieldInput("Size (field)", info.size,
                [&](uint16_t v){ info.size = v; }, ImGuiDataType_U16);
            ImGui::SameLine();
            ImGui::Text("= %u bytes", sii.eepromSizeBytes());

            ImGui::SetNextItemWidth(100.0f);
            changed |= fieldInput("Version", info.version,
                [&](uint16_t v){ info.version = v; }, ImGuiDataType_U16);
        }

        return changed;
    }
}
