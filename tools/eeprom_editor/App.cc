#include <cstdio>
#include <fstream>
#include <iterator>

#include <imgui.h>
#include <portable-file-dialogs.h>

#include "kickcat/EEPROM/EEPROM_factory.h"

#include "App.h"
#include "Editors.h"

namespace kickcat::eeprom_editor
{
    constexpr float SIDEBAR_WIDTH = 220.0f;

    struct CategoryInfo
    {
        Category id;
        char const* label;
    };

    constexpr CategoryInfo CATEGORIES[] =
    {
        { Category::Info,         "Info (Header)"  },
        { Category::Strings,      "Strings"        },
        { Category::General,      "General"        },
        { Category::SyncManagers, "Sync Managers"  },
        { Category::FMMU,         "FMMU"           },
        { Category::TxPDO,        "TxPDO"          },
        { Category::RxPDO,        "RxPDO"          },
    };

    App::App()
    {
        mem_edit_.ReadOnly = true;
        mem_edit_.OptShowOptions = false;
        mem_edit_.OptShowDataPreview = false;
        newFile();
    }

    void App::render()
    {
        renderMenuBar();

        auto const avail = ImGui::GetContentRegionAvail();
        float const status_bar_height = ImGui::GetFrameHeight();
        float const hex_view_height   = 200.0f;
        float const content_height    = avail.y - hex_view_height - status_bar_height;

        // Top region: sidebar + content panel
        ImGui::BeginChild("##top_region", ImVec2(0, content_height), ImGuiChildFlags_None);
        {
            ImGui::BeginChild("##sidebar", ImVec2(SIDEBAR_WIDTH, 0),
                              ImGuiChildFlags_Borders);
            renderSidebar();
            ImGui::EndChild();

            ImGui::SameLine();

            ImGui::BeginChild("##content", ImVec2(0, 0), ImGuiChildFlags_Borders);
            renderContentPanel();
            ImGui::EndChild();
        }
        ImGui::EndChild();

        // Hex view + options side panel
        ImGui::BeginChild("##hex_view", ImVec2(0, hex_view_height), ImGuiChildFlags_Borders);
        {
            constexpr float OPTIONS_PANEL_WIDTH = 220.0f;
            float hex_width = ImGui::GetContentRegionAvail().x - OPTIONS_PANEL_WIDTH;

            ImGui::BeginChild("##hex_editor", ImVec2(hex_width, 0));
            if (not serialized_.empty())
            {
                mem_edit_.DrawContents(serialized_.data(), serialized_.size());
            }
            ImGui::EndChild();

            ImGui::SameLine();

            ImGui::BeginChild("##hex_options", ImVec2(0, 0));
            renderHexPanel();
            ImGui::EndChild();
        }
        ImGui::EndChild();

        renderStatusBar();
    }

    void App::newFile()
    {
        sii_ = eeprom::SII{};
        createMinimalEEPROM(sii_.info);
        sii_.info.crc = eeprom::computeInfoCRC(sii_.info);
        serialized_ = sii_.serialize();
        file_path_.clear();
        modified_ = false;
    }

    void App::openFile()
    {
        auto selection = pfd::open_file("Open EEPROM binary", ".",
                                        {"EEPROM files", "*.bin *.eep", "All files", "*"}).result();
        if (not selection.empty())
        {
            openFilePath(selection[0]);
        }
    }

    void App::saveFile()
    {
        if (file_path_.empty())
        {
            saveFileAs();
        }
        else
        {
            saveFilePath(file_path_);
        }
    }

    void App::saveFileAs()
    {
        auto dest = pfd::save_file("Save EEPROM binary", ".",
                                   {"EEPROM files", "*.bin *.eep", "All files", "*"}).result();
        if (not dest.empty())
        {
            saveFilePath(dest);
        }
    }

    void App::openFilePath(std::string const& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (not file)
        {
            pfd::message("Error", "Cannot open file: " + path,
                         pfd::choice::ok, pfd::icon::error);
            return;
        }

        std::vector<uint8_t> data{std::istreambuf_iterator<char>(file),
                                   std::istreambuf_iterator<char>()};

        try
        {
            sii_ = eeprom::SII{};
            sii_.parse(data);
            serialized_ = sii_.serialize();
            file_path_  = path;
            modified_   = false;
        }
        catch (std::exception const& e)
        {
            pfd::message("Parse error", std::string("Failed to parse EEPROM:\n") + e.what(),
                         pfd::choice::ok, pfd::icon::error);
            newFile();
        }
    }

    void App::saveFilePath(std::string const& path)
    {
        serialized_ = sii_.serialize();

        std::ofstream file(path, std::ios::binary);
        if (not file)
        {
            pfd::message("Error", "Cannot write file: " + path,
                         pfd::choice::ok, pfd::icon::error);
            return;
        }

        file.write(reinterpret_cast<char const*>(serialized_.data()),
                   static_cast<std::streamsize>(serialized_.size()));
        file_path_ = path;
        modified_  = false;
    }

    void App::renderMenuBar()
    {
        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("New", "Ctrl+N"))
                {
                    newFile();
                }
                if (ImGui::MenuItem("Open...", "Ctrl+O"))
                {
                    openFile();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Save", "Ctrl+S"))
                {
                    saveFile();
                }
                if (ImGui::MenuItem("Save As...", "Ctrl+Shift+S"))
                {
                    saveFileAs();
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Quit", "Ctrl+Q"))
                {
                    std::exit(0);
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Edit"))
            {
                if (ImGui::MenuItem("Recompute CRC"))
                {
                    sii_.info.crc = eeprom::computeInfoCRC(sii_.info);
                    serialized_   = sii_.serialize();
                    modified_     = true;
                }
                ImGui::EndMenu();
            }

            float title_width = ImGui::CalcTextSize("KickCAT EEPROM Editor").x;
            ImGui::SameLine(ImGui::GetWindowWidth() - title_width - 16.0f);
            ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 1.0f), "KickCAT EEPROM Editor");

            ImGui::EndMenuBar();
        }

        // Keyboard shortcuts
        auto const& io = ImGui::GetIO();
        if (io.KeyCtrl and ImGui::IsKeyPressed(ImGuiKey_N))
        {
            newFile();
        }
        if (io.KeyCtrl and ImGui::IsKeyPressed(ImGuiKey_O))
        {
            openFile();
        }
        if (io.KeyCtrl and ImGui::IsKeyPressed(ImGuiKey_S))
        {
            if (io.KeyShift)
            {
                saveFileAs();
            }
            else
            {
                saveFile();
            }
        }
        if (io.KeyCtrl and ImGui::IsKeyPressed(ImGuiKey_Q))
        {
            std::exit(0);
        }
    }

    void App::renderSidebar()
    {
        ImGui::TextColored(ImVec4(0.65f, 0.65f, 0.65f, 1.0f), "CATEGORIES");
        ImGui::Separator();

        for (auto const& cat : CATEGORIES)
        {
            bool selected = (cat.id == active_category_);

            if (ImGui::Selectable(cat.label, selected, ImGuiSelectableFlags_None, ImVec2(0, 24)))
            {
                active_category_ = cat.id;
            }
        }
    }

    void App::renderContentPanel()
    {
        bool changed = false;

        switch (active_category_)
        {
            case Category::Info:         { changed = info::render(sii_);                    break; }
            case Category::Strings:      { changed = strings::render(sii_);                 break; }
            case Category::General:      { changed = general::render(sii_);                 break; }
            case Category::SyncManagers: { changed = syncm::render(sii_);                   break; }
            case Category::FMMU:         { changed = fmmu::render(sii_);                    break; }
            case Category::TxPDO:        { changed = pdo::render(sii_, pdo::Direction::Tx); break; }
            case Category::RxPDO:        { changed = pdo::render(sii_, pdo::Direction::Rx); break; }
        }

        if (changed)
        {
            modified_   = true;
            serialized_ = sii_.serialize();
        }
    }

    void App::renderHexPanel()
    {
        ImGui::TextColored(ImVec4(0.65f, 0.65f, 0.65f, 1.0f), "Display");
        ImGui::Separator();

        if (ImGui::DragInt("Columns", &mem_edit_.Cols, 0.2f, 4, 32, "%d"))
        {
            mem_edit_.ContentsWidthChanged = true;
        }
        if (ImGui::Checkbox("ASCII", &mem_edit_.OptShowAscii))
        {
            mem_edit_.ContentsWidthChanged = true;
        }
        ImGui::Checkbox("HexII", &mem_edit_.OptShowHexII);
        ImGui::Checkbox("Grey zeroes", &mem_edit_.OptGreyOutZeroes);
        ImGui::Checkbox("Uppercase", &mem_edit_.OptUpperCaseHex);

        ImGui::Spacing();
        ImGui::TextColored(ImVec4(0.65f, 0.65f, 0.65f, 1.0f), "Data Preview");
        ImGui::Separator();

        if (mem_edit_.DataPreviewAddr != static_cast<size_t>(-1) and not serialized_.empty())
        {
            MemoryEditor::Sizes s;
            mem_edit_.CalcSizes(s, serialized_.size(), 0);
            mem_edit_.DrawPreviewLine(s, serialized_.data(), serialized_.size(), 0);
        }
        else
        {
            ImGui::TextDisabled("Click a byte to preview");
        }
    }

    void App::renderStatusBar()
    {
        ImGui::Separator();

        if (file_path_.empty())
        {
            ImGui::Text("[new file]");
        }
        else
        {
            ImGui::Text("%s", file_path_.c_str());
        }

        if (modified_)
        {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.88f, 0.75f, 0.31f, 1.0f), "(modified)");
        }

        ImGui::SameLine();
        ImGui::Text("|");
        ImGui::SameLine();

        auto const size_bytes = serialized_.size();
        ImGui::Text("%zu bytes (%zu words)", size_bytes, size_bytes / 2);

        ImGui::SameLine();
        ImGui::Text("|");
        ImGui::SameLine();

        uint8_t actual_crc   = static_cast<uint8_t>(sii_.info.crc);
        uint8_t expected_crc = static_cast<uint8_t>(eeprom::computeInfoCRC(sii_.info));

        if (actual_crc == expected_crc)
        {
            ImGui::TextColored(ImVec4(0.31f, 0.79f, 0.41f, 1.0f),
                               "CRC 0x%02X OK", actual_crc);
        }
        else
        {
            ImGui::TextColored(ImVec4(0.88f, 0.33f, 0.33f, 1.0f),
                               "CRC 0x%02X != 0x%02X", actual_crc, expected_crc);
        }
    }
}
