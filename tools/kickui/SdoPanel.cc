#include "SdoPanel.h"

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "imgui.h"

#include "kickcat/Bus.h"
#include "kickcat/CoE/OD.h"

#include "BusSession.h"
#include "Theme.h"

namespace kickcat::kickui
{
    namespace
    {
        struct TypeInfo
        {
            char const* label;
            int         width;   // bytes; 0 = string/raw
            bool        is_signed;
            bool        is_string;
            bool        is_raw;
            bool        is_real;
        };

        TypeInfo const TYPES[] = {
            {"UNSIGNED8",       1, false, false, false, false},
            {"UNSIGNED16",      2, false, false, false, false},
            {"UNSIGNED32",      4, false, false, false, false},
            {"UNSIGNED64",      8, false, false, false, false},
            {"INTEGER8",        1, true,  false, false, false},
            {"INTEGER16",       2, true,  false, false, false},
            {"INTEGER32",       4, true,  false, false, false},
            {"INTEGER64",       8, true,  false, false, false},
            {"REAL32",          4, false, false, false, true},
            {"REAL64",          8, false, false, false, true},
            {"VISIBLE_STRING",  0, false, true,  false, false},
            {"RAW (hex)",       0, false, false, true,  false},
        };

        // Map a CoE DataType (from OD discovery) to a TYPES index; RAW fallback.
        int typeIndexFor(uint16_t data_type)
        {
            switch (static_cast<CoE::DataType>(data_type))
            {
                case CoE::DataType::UNSIGNED8:      { return 0;  }
                case CoE::DataType::UNSIGNED16:     { return 1;  }
                case CoE::DataType::UNSIGNED32:     { return 2;  }
                case CoE::DataType::UNSIGNED64:     { return 3;  }
                case CoE::DataType::INTEGER8:       { return 4;  }
                case CoE::DataType::INTEGER16:      { return 5;  }
                case CoE::DataType::INTEGER32:      { return 6;  }
                case CoE::DataType::INTEGER64:      { return 7;  }
                case CoE::DataType::REAL32:         { return 8;  }
                case CoE::DataType::REAL64:         { return 9;  }
                case CoE::DataType::VISIBLE_STRING: { return 10; }
                default:                            { return 11; }
            }
        }

        // The combo order must match Bus::Access (the index is sent verbatim).
        char const* const ACCESS_LABELS[] = {"PARTIAL", "COMPLETE", "EMULATE_COMPLETE"};
        static_assert(static_cast<int>(Bus::Access::PARTIAL) == 0, "access combo order");
        static_assert(static_cast<int>(Bus::Access::COMPLETE) == 1, "access combo order");
        static_assert(static_cast<int>(Bus::Access::EMULATE_COMPLETE) == 2, "access combo order");

        std::string hexDump(std::vector<uint8_t> const& data)
        {
            std::string out;
            char byte[4];
            for (auto b : data)
            {
                std::snprintf(byte, sizeof(byte), "%02X ", b);
                out += byte;
            }
            return out;
        }

        std::string formatValue(TypeInfo const& t, std::vector<uint8_t> const& data)
        {
            if (data.empty())
            {
                return "(empty)";
            }
            if (t.is_string)
            {
                return std::string(data.begin(), data.end());
            }
            if (t.is_raw or (t.width == 0))
            {
                return hexDump(data);
            }

            if (t.is_real)
            {
                char rbuf[64];
                if ((t.width == 4) and (data.size() >= 4))
                {
                    float f = 0.0f;
                    std::memcpy(&f, data.data(), 4);
                    std::snprintf(rbuf, sizeof(rbuf), "%g", static_cast<double>(f));
                    return rbuf;
                }
                if ((t.width == 8) and (data.size() >= 8))
                {
                    double d = 0.0;
                    std::memcpy(&d, data.data(), 8);
                    std::snprintf(rbuf, sizeof(rbuf), "%g", d);
                    return rbuf;
                }
                return hexDump(data);
            }

            uint64_t raw = 0;
            int width = t.width;
            if (width > static_cast<int>(data.size()))
            {
                width = static_cast<int>(data.size());
            }
            for (int i = 0; i < width; ++i)
            {
                raw |= static_cast<uint64_t>(data[i]) << (8 * i);
            }

            char buf[64];
            if (t.is_signed)
            {
                int64_t sval = static_cast<int64_t>(raw);
                int shift = 64 - 8 * width;
                // shift unsigned: a signed left-shift into the sign bit is UB. The
                // guard drops width 0 and 8, whose shift counts are out of range.
                if (shift > 0 and shift < 64)
                {
                    sval = static_cast<int64_t>(raw << shift) >> shift;
                }
                std::snprintf(buf, sizeof(buf), "%lld  (0x%llX)",
                              static_cast<long long>(sval), static_cast<unsigned long long>(raw));
            }
            else
            {
                std::snprintf(buf, sizeof(buf), "%llu  (0x%llX)",
                              static_cast<unsigned long long>(raw), static_cast<unsigned long long>(raw));
            }
            return buf;
        }

        std::vector<uint8_t> bytesFromHex(char const* text)
        {
            std::vector<uint8_t> out;
            char const* p = text;
            while (*p != '\0')
            {
                if ((*p == ' ') or (*p == ',') or (*p == ':') or (*p == '\t'))
                {
                    ++p;
                    continue;
                }
                if ((p[0] == '0') and ((p[1] == 'x') or (p[1] == 'X')))
                {
                    p += 2;
                    continue;
                }
                if (std::isxdigit(static_cast<unsigned char>(*p)) == 0)
                {
                    ++p;  // skip a stray non-hex char rather than spin or misparse
                    continue;
                }
                char pair[3] = {*p, '\0', '\0'};
                ++p;
                if (std::isxdigit(static_cast<unsigned char>(*p)) != 0)
                {
                    pair[1] = *p;
                    ++p;
                }
                out.push_back(static_cast<uint8_t>(std::strtol(pair, nullptr, 16)));
            }
            return out;
        }

        std::vector<uint8_t> bytesFromValue(TypeInfo const& t, char const* text)
        {
            if (t.is_raw)
            {
                return bytesFromHex(text);
            }
            if (t.is_string)
            {
                return std::vector<uint8_t>(text, text + std::strlen(text));
            }
            if (t.is_real)
            {
                std::vector<uint8_t> out(t.width);
                double d = std::strtod(text, nullptr);
                if (t.width == 4)
                {
                    float f = static_cast<float>(d);
                    std::memcpy(out.data(), &f, 4);
                }
                else
                {
                    std::memcpy(out.data(), &d, 8);
                }
                return out;
            }
            long long v = std::strtoll(text, nullptr, 0);
            std::vector<uint8_t> out(t.width);
            for (int i = 0; i < t.width; ++i)
            {
                out[i] = static_cast<uint8_t>((static_cast<unsigned long long>(v) >> (8 * i)) & 0xFF);
            }
            return out;
        }

        uint16_t parseIndex(char const* text)
        {
            return static_cast<uint16_t>(std::strtol(text, nullptr, 0));
        }
    }

    bool SdoPanel::appliesTo(Device const& device) const
    {
        return device.has_coe;
    }

    void SdoPanel::render(BusSession&, Device& device)
    {
        if (not device.sdoAvailable())
        {
            ImGui::TextDisabled("Connect to a bus to use SDO.");
            return;
        }

        TypeInfo const& type = TYPES[type_];
        bool transfer_busy = (last_ and not last_->done);

        // --- read / write ---
        ImGui::SeparatorText("SDO transfer");
        ImGui::SetNextItemWidth(px(110.0f));
        ImGui::InputText("Index", index_buf_, sizeof(index_buf_));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(px(80.0f));
        ImGui::InputInt("Sub", &subindex_);
        if (subindex_ < 0)   { subindex_ = 0; }
        if (subindex_ > 255) { subindex_ = 255; }

        ImGui::SetNextItemWidth(px(160.0f));
        ImGui::Combo("Access", &access_, ACCESS_LABELS, IM_ARRAYSIZE(ACCESS_LABELS));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(px(150.0f));
        if (ImGui::BeginCombo("Type", type.label))
        {
            for (int i = 0; i < IM_ARRAYSIZE(TYPES); ++i)
            {
                bool selected = (i == type_);
                if (ImGui::Selectable(TYPES[i].label, selected))
                {
                    type_ = i;
                }
            }
            ImGui::EndCombo();
        }

        // Gate submissions while a transfer is pending: the result is single-slot
        // (last_), so a second click would orphan the in-flight result and queue a
        // competing mailbox command with no feedback.
        ImGui::BeginDisabled(transfer_busy);
        if (ImGui::Button("Read"))
        {
            last_type_ = type_;  // freeze the interpretation used for this result
            last_ = device.readSDO(parseIndex(index_buf_),
                                    static_cast<uint8_t>(subindex_), access_);
        }
        ImGui::SameLine();
        if (ImGui::Button("Write value"))
        {
            last_ = device.writeSDO(parseIndex(index_buf_),
                                     static_cast<uint8_t>(subindex_), access_,
                                     bytesFromValue(type, value_buf_));
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##value", value_buf_, sizeof(value_buf_));

        // --- raw "absolute" entry ---
        ImGui::SeparatorText("Absolute (raw bytes, sent verbatim)");
        ImGui::SetNextItemWidth(-px(90.0f));
        ImGui::InputTextWithHint("##raw", "e.g. 0F 00 7A 60", raw_buf_, sizeof(raw_buf_));
        ImGui::SameLine();
        ImGui::BeginDisabled(transfer_busy);
        if (ImGui::Button("Write raw"))
        {
            last_ = device.writeSDO(parseIndex(index_buf_),
                                     static_cast<uint8_t>(subindex_), access_,
                                     bytesFromHex(raw_buf_));
        }
        ImGui::EndDisabled();

        // --- result ---
        if (last_)
        {
            if (not last_->done)
            {
                ImGui::TextDisabled("transfer in progress...");
            }
            else if (last_->ok)
            {
                ImGui::TextColored(COLOR_GREEN, "%s", last_->message.c_str());
                if (not last_->data.empty())
                {
                    ImGui::Text("value: %s", formatValue(TYPES[last_type_], last_->data).c_str());
                    ImGui::TextDisabled("bytes: %s", hexDump(last_->data).c_str());
                }
            }
            else
            {
                ImGui::TextColored(COLOR_RED, "%s", last_->message.c_str());
            }
        }

        // --- object dictionary discovery ---
        ImGui::SeparatorText("Object dictionary");
        OdScan const scan = device.odScan();   // one coherent scan per frame
        ImGui::BeginDisabled(scan.running);
        if (ImGui::Button("Discover OD"))
        {
            device.discoverOD();
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (scan.running)
        {
            ImGui::Text("scanning %d / %d...", scan.count, scan.total);
        }
        else if (scan.scanned)
        {
            if ((scan.total > 0) and (scan.count < scan.total))
            {
                // The scan was aborted (e.g. Back-to-PRE-OP mid-scan): say so instead
                // of showing the partial count as if it were the whole dictionary.
                ImGui::TextColored(COLOR_YELLOW, "%d / %d objects (incomplete \xe2\x80\x94 re-scan)",
                                   scan.count, scan.total);
            }
            else
            {
                ImGui::TextDisabled("%d objects", scan.count);
            }
        }
        else
        {
            ImGui::TextDisabled("not discovered");
        }

        if (not scan.error.empty())
        {
            ImGui::TextColored(COLOR_RED, "%s", scan.error.c_str());
        }

        ImGui::SetNextItemWidth(px(200.0f));
        ImGui::InputTextWithHint("##odfilter", "filter by name/index", filter_buf_, sizeof(filter_buf_));

        if (scan.scanned)
        {
            renderOdTable(scan.objects);
        }
    }

    void SdoPanel::renderOdTable(std::vector<OdObject> const& objects)
    {
        if (objects.empty())
        {
            return;
        }

        std::string filter = filter_buf_;
        for (auto& c : filter)
        {
            c = static_cast<char>(std::tolower(c));
        }

        ImGui::TextDisabled("Access: ro/rw/wo = read-only/read-write/write-only; "
                            "Rx/Tx = PDO-mappable (hover a cell for per-state detail)");
        if (not ImGui::BeginTable("##od", 6,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                ImVec2(0.0f, ImGui::GetContentRegionAvail().y)))  // fill remaining height
        {
            return;
        }
        ImGui::TableSetupColumn("Index",  ImGuiTableColumnFlags_WidthFixed, px(64.0f));
        ImGui::TableSetupColumn("Name",   ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Type",   ImGuiTableColumnFlags_WidthFixed, px(120.0f));
        ImGui::TableSetupColumn("Sub",    ImGuiTableColumnFlags_WidthFixed, px(36.0f));
        ImGui::TableSetupColumn("Access", ImGuiTableColumnFlags_WidthFixed, px(72.0f));
        ImGui::TableSetupColumn("Value",  ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();

        // Access bits -> "ro"/"rw"/"wo" plus PDO mappability.
        auto accessStr = [](uint16_t a) -> std::string
        {
            bool r = (a & CoE::Access::READ)  != 0;
            bool w = (a & CoE::Access::WRITE) != 0;
            std::string s = "--";
            if (r and w) { s = "rw"; }
            else if (r)  { s = "ro"; }
            else if (w)  { s = "wo"; }
            if (a & CoE::Access::RxPDO) { s += " Rx"; }
            if (a & CoE::Access::TxPDO) { s += " Tx"; }
            return s;
        };
        // Compact code in the cell, full per-state breakdown on hover.
        auto accessCell = [&](uint16_t a)
        {
            ImGui::TextUnformatted(accessStr(a).c_str());
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("%s", CoE::Access::toString(a).c_str());
            }
        };

        // Value cell: formatted value, else the read error, else a write-only note.
        auto valueCell = [&](uint16_t data_type, std::vector<uint8_t> const& value,
                             std::string const& err, uint16_t access)
        {
            if (not value.empty())
            {
                ImGui::TextUnformatted(formatValue(TYPES[typeIndexFor(data_type)], value).c_str());
            }
            else if (not err.empty())
            {
                ImGui::TextColored(COLOR_RED, "%s", err.c_str());
            }
            else if ((access & CoE::Access::WRITE) and not (access & CoE::Access::READ))
            {
                ImGui::TextDisabled("(write-only)");
            }
        };

        for (auto const& obj : objects)
        {
            if (not filter.empty())
            {
                char idx_hex[8];
                std::snprintf(idx_hex, sizeof(idx_hex), "%04x", obj.index);
                std::string name_lower = obj.name;
                for (auto& c : name_lower)
                {
                    c = static_cast<char>(std::tolower(c));
                }
                if ((name_lower.find(filter) == std::string::npos)
                    and (std::string(idx_hex).find(filter) == std::string::npos))
                {
                    continue;
                }
            }

            bool complex = (obj.object_code == static_cast<uint8_t>(CoE::ObjectCode::ARRAY))
                        or (obj.object_code == static_cast<uint8_t>(CoE::ObjectCode::RECORD));

            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            char label[16];
            std::snprintf(label, sizeof(label), "0x%04X", obj.index);
            if (ImGui::Selectable(label, false, ImGuiSelectableFlags_SpanAllColumns))
            {
                std::snprintf(index_buf_, sizeof(index_buf_), "0x%04X", obj.index);
                subindex_ = 0;
                type_ = typeIndexFor(obj.data_type);  // prefill the discovered type
            }
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(obj.name.c_str());
            ImGui::TableNextColumn();
            if (complex) { ImGui::TextUnformatted(CoE::toString(static_cast<CoE::ObjectCode>(obj.object_code))); }
            else         { ImGui::TextUnformatted(CoE::toString(static_cast<CoE::DataType>(obj.data_type))); }
            ImGui::TableNextColumn();
            ImGui::Text("%u", obj.max_subindex);
            ImGui::TableNextColumn();
            accessCell(obj.access);
            ImGui::TableNextColumn();
            valueCell(obj.data_type, obj.value, obj.value_error, obj.access);

            // RECORD/ARRAY sub-entries, indented under the object.
            for (auto const& se : obj.entries)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                char slabel[24];
                std::snprintf(slabel, sizeof(slabel), "  .%u##%04x.%u", se.subindex, obj.index, se.subindex);
                if (ImGui::Selectable(slabel, false, ImGuiSelectableFlags_SpanAllColumns))
                {
                    std::snprintf(index_buf_, sizeof(index_buf_), "0x%04X", obj.index);
                    subindex_ = se.subindex;
                    type_ = typeIndexFor(se.data_type);
                }
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(se.name.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(CoE::toString(static_cast<CoE::DataType>(se.data_type)));
                ImGui::TableNextColumn();
                ImGui::Text("%u", se.subindex);
                ImGui::TableNextColumn();
                accessCell(se.access);
                ImGui::TableNextColumn();
                valueCell(se.data_type, se.value, se.value_error, se.access);
            }
        }
        ImGui::EndTable();
    }
}
