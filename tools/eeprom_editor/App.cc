#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <system_error>

#ifdef __linux__
    #include <climits>
    #include <unistd.h>
#endif

#include <imgui.h>
#include <portable-file-dialogs.h>

#include "kickcat/EEPROM/EEPROM_factory.h"
#include "kickcat/Bus.h"
#include "kickcat/Link.h"
#include "kickcat/SocketNull.h"

#ifdef __linux__
    #include "kickcat/OS/Linux/Socket.h"
#elif __MINGW64__
    #include "kickcat/OS/Windows/Socket.h"
#endif

#include "App.h"
#include "Editors.h"

namespace kickcat::eeprom_editor
{
    constexpr float SIDEBAR_WIDTH = 220.0f;

    static void statusSeparator()
    {
        ImGui::SameLine();
        ImGui::Text("|");
        ImGui::SameLine();
    }

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

    App::~App()
    {
        joinWorker();
    }

    void App::render()
    {
        finalizeWorker();
        renderMenuBar();

        bool const busy = isBusy();

        auto const avail = ImGui::GetContentRegionAvail();
        float const status_bar_height = ImGui::GetFrameHeight();
        float const hex_view_height   = 200.0f;
        float const content_height    = avail.y - hex_view_height - status_bar_height;

        if (busy) { ImGui::BeginDisabled(); }

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

        if (busy) { ImGui::EndDisabled(); }

        renderStatusBar();

        // Modal dialogs (rendered as overlays)
        renderConnectDialog();
        renderSlaveDialog();
        renderPrivilegeDialog();
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

            if (ImGui::BeginMenu("Device"))
            {
                bool busy = isBusy();

                if (ImGui::MenuItem("Connect...", nullptr, false, not isConnected() and not busy))
                {
                    device_error_.clear();
                    cached_interfaces_ = listInterfaces();
                    selected_slave_index_ = -1;
                    show_connect_dialog_ = true;
                }
                if (ImGui::MenuItem("Disconnect", nullptr, false, isConnected() and not busy))
                {
                    disconnect();
                }
                ImGui::Separator();
                bool has_slaves = isConnected() and not bus_->slaves().empty() and not busy;
                if (ImGui::MenuItem("Load from Slave...", nullptr, false, has_slaves))
                {
                    selected_slave_index_ = -1;
                    device_error_.clear();
                    slave_action_ = SlaveAction::Load;
                    show_slave_dialog_ = true;
                }
                if (ImGui::MenuItem("Flash to Slave...", nullptr, false, has_slaves))
                {
                    selected_slave_index_ = -1;
                    device_error_.clear();
                    slave_action_ = SlaveAction::Flash;
                    show_slave_dialog_ = true;
                }
                ImGui::EndMenu();
            }

            float title_width = ImGui::CalcTextSize("KickCAT EEPROM Editor").x;
            ImGui::SameLine(ImGui::GetWindowWidth() - title_width - 16.0f);
            ImGui::TextColored(COLOR_DIM, "KickCAT EEPROM Editor");

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
        ImGui::TextColored(COLOR_GREY, "CATEGORIES");
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
        ImGui::TextColored(COLOR_GREY, "Display");
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
        ImGui::TextColored(COLOR_GREY, "Data Preview");
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
            ImGui::TextColored(COLOR_YELLOW, "(modified)");
        }

        statusSeparator();

        auto const size_bytes = serialized_.size();
        ImGui::Text("%zu bytes (%zu words)", size_bytes, size_bytes / 2);

        statusSeparator();

        uint8_t actual_crc   = static_cast<uint8_t>(sii_.info.crc);
        uint8_t expected_crc = static_cast<uint8_t>(eeprom::computeInfoCRC(sii_.info));

        if (actual_crc == expected_crc)
        {
            ImGui::TextColored(COLOR_GREEN,
                               "CRC 0x%02X OK", actual_crc);
        }
        else
        {
            ImGui::TextColored(COLOR_RED,
                               "CRC 0x%02X != 0x%02X", actual_crc, expected_crc);
        }

        statusSeparator();

        if (isBusy())
        {
            std::lock_guard lock(worker_status_mutex_);
            ImGui::TextColored(COLOR_BLUE, "%s", worker_status_.c_str());
            ImGui::SameLine();
            ImGui::ProgressBar(worker_progress_.load(), ImVec2(150, ImGui::GetFrameHeight()));
        }
        else if (not worker_error_.empty())
        {
            ImGui::TextColored(COLOR_RED, "%s", worker_error_.c_str());
        }
        else if (isConnected())
        {
            ImGui::TextColored(COLOR_GREEN,
                               "Connected: %s (%d slaves)",
                               connected_interface_.c_str(),
                               static_cast<int>(bus_->slaves().size()));
        }
        else
        {
            ImGui::TextDisabled("Not connected");
        }
    }

    // ── Device operations ──────────────────────────────────────────────

    bool App::isConnected() const
    {
        return bus_ != nullptr;
    }

    bool App::isBusy() const
    {
        return worker_.joinable() and not worker_done_.load();
    }

    void App::joinWorker()
    {
        if (worker_.joinable())
        {
            worker_.join();
        }
    }

    void App::finalizeWorker()
    {
        if (worker_.joinable() and worker_done_.load())
        {
            worker_.join();
        }
    }

    void App::connectToInterface(std::string const& name)
    {
        joinWorker();

        show_connect_dialog_ = false;
        needs_privilege_escalation_ = false;
        worker_progress_ = 0.0f;
        worker_done_ = false;
        {
            std::lock_guard lock(worker_status_mutex_);
            worker_status_ = "Initializing...";
            worker_error_.clear();
        }

        std::string interface_name = name;
        worker_ = std::thread([this, interface_name]()
        {
            try
            {
                auto socket_nominal = std::make_shared<Socket>();
                socket_nominal->open(interface_name);

                auto socket_redundancy = std::make_shared<SocketNull>();
                auto report_redundancy = [](){};

                auto link = std::make_shared<Link>(socket_nominal, socket_redundancy, report_redundancy);
                link->setTimeout(2ms);

                auto bus = std::make_unique<Bus>(link);
                bus->configureWaitLatency(1ms, 10ms);

                {
                    std::lock_guard lock(worker_status_mutex_);
                    worker_status_ = "Detecting slaves...";
                }
                worker_progress_ = 0.25f;

                if (bus->detectSlaves() == 0)
                {
                    std::lock_guard lock(worker_status_mutex_);
                    worker_error_ = "No slaves detected on " + interface_name;
                    worker_done_ = true;
                    return;
                }

                uint16_t param = 0x0;
                bus->broadcastWrite(reg::EEPROM_CONFIG, &param, 2);
                bus->setAddresses();

                {
                    std::lock_guard lock(worker_status_mutex_);
                    worker_status_ = "Reading EEPROMs...";
                }
                worker_progress_ = 0.5f;

                bus->fetchEeprom();

                // Success: transfer ownership to app (on main thread via done flag)
                link_ = std::move(link);
                bus_ = std::move(bus);
                connected_interface_ = interface_name;

                worker_progress_ = 1.0f;
                worker_done_ = true;
            }
            catch (std::system_error const& e)
            {
                std::lock_guard lock(worker_status_mutex_);
                if (e.code().value() == EPERM or e.code().value() == EACCES)
                {
                    needs_privilege_escalation_ = true;
                    worker_error_ = "Permission denied: raw socket requires elevated privileges.";
                }
                else
                {
                    worker_error_ = std::string("Connection failed: ") + e.what();
                }
                worker_done_ = true;
            }
            catch (std::exception const& e)
            {
                std::lock_guard lock(worker_status_mutex_);
                worker_error_ = std::string("Connection failed: ") + e.what();
                worker_done_ = true;
            }
        });
    }

    void App::disconnect()
    {
        joinWorker();
        bus_.reset();
        link_.reset();
        connected_interface_.clear();
        worker_error_.clear();
    }

    void App::loadFromSlave(int slave_index)
    {
        try
        {
            auto& slave = bus_->slaves().at(slave_index);
            auto data = slave.sii.serialize();

            sii_ = eeprom::SII{};
            sii_.parse(data);
            serialized_ = sii_.serialize();
            file_path_.clear();
            modified_ = false;

            show_slave_dialog_ = false;
        }
        catch (std::exception const& e)
        {
            device_error_ = std::string("Failed to load EEPROM: ") + e.what();
        }
    }

    void App::flashToSlave(int slave_index)
    {
        joinWorker();

        show_slave_dialog_ = false;
        worker_progress_ = 0.0f;
        worker_done_ = false;
        {
            std::lock_guard lock(worker_status_mutex_);
            worker_status_ = "Flashing EEPROM...";
            worker_error_.clear();
        }

        // Serialize on the main thread before starting the worker
        serialized_ = sii_.serialize();
        std::vector<uint16_t> buffer(serialized_.size() / 2);
        std::memcpy(buffer.data(), serialized_.data(), serialized_.size());

        worker_ = std::thread([this, slave_index, buffer = std::move(buffer)]()
        {
            try
            {
                auto& slave = bus_->slaves().at(slave_index);

                {
                    std::lock_guard lock(worker_status_mutex_);
                    worker_status_ = "Writing EEPROM...";
                }

                for (uint32_t i = 0; i < buffer.size(); ++i)
                {
                    // writeEeprom takes a void* — use a local copy to avoid const issues
                    uint16_t word = buffer[i];
                    bus_->writeEeprom(slave, static_cast<uint16_t>(i), &word, 2);
                    worker_progress_ = static_cast<float>(i + 1) / static_cast<float>(buffer.size());
                }

                worker_done_ = true;
            }
            catch (std::exception const& e)
            {
                std::lock_guard lock(worker_status_mutex_);
                worker_error_ = std::string("Flash failed at ") +
                                std::to_string(static_cast<int>(worker_progress_.load() * 100)) +
                                "%: " + e.what();
                worker_done_ = true;
            }
        });
    }

    // ── Dialog rendering ───────────────────────────────────────────────

    void App::renderConnectDialog()
    {
        if (show_connect_dialog_)
        {
            ImGui::OpenPopup("Connect to EtherCAT Network");
        }

        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(450, 350), ImGuiCond_Appearing);

        if (ImGui::BeginPopupModal("Connect to EtherCAT Network", &show_connect_dialog_))
        {
            ImGui::Text("Select a network interface:");
            ImGui::Separator();

            if (ImGui::BeginChild("##iface_list", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2.5f)))
            {
                for (int i = 0; i < static_cast<int>(cached_interfaces_.size()); ++i)
                {
                    auto const& iface = cached_interfaces_[i];
                    char label[256];
                    std::snprintf(label, sizeof(label), "%s  (%s)", iface.name.c_str(), iface.description.c_str());

                    bool is_selected = (selected_slave_index_ == i);
                    if (ImGui::Selectable(label, is_selected, ImGuiSelectableFlags_AllowDoubleClick))
                    {
                        selected_slave_index_ = i;
                        if (ImGui::IsMouseDoubleClicked(0))
                        {
                            connectToInterface(cached_interfaces_[i].name);
                        }
                    }
                }
            }
            ImGui::EndChild();

            if (not device_error_.empty())
            {
                ImGui::TextColored(COLOR_RED, "%s", device_error_.c_str());
            }

            bool can_connect = (selected_slave_index_ >= 0 and
                                selected_slave_index_ < static_cast<int>(cached_interfaces_.size()));
            if (not can_connect) { ImGui::BeginDisabled(); }
            if (ImGui::Button("Connect", ImVec2(120, 0)))
            {
                connectToInterface(cached_interfaces_[selected_slave_index_].name);
            }
            if (not can_connect) { ImGui::EndDisabled(); }

            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                show_connect_dialog_ = false;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    bool App::renderSlaveTable()
    {
        if (not isConnected())
        {
            return false;
        }

        bool double_clicked = false;
        auto& slaves = bus_->slaves();

        if (ImGui::BeginTable("##slaves", 5,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 3.0f)))
        {
            ImGui::TableSetupColumn("#",            ImGuiTableColumnFlags_WidthFixed, 30.0f);
            ImGui::TableSetupColumn("Address",      ImGuiTableColumnFlags_WidthFixed, 60.0f);
            ImGui::TableSetupColumn("Vendor ID",    ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("Product Code", ImGuiTableColumnFlags_WidthFixed, 100.0f);
            ImGui::TableSetupColumn("Name",         ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            for (int i = 0; i < static_cast<int>(slaves.size()); ++i)
            {
                auto const& slave = slaves[i];
                ImGui::TableNextRow();

                ImGui::TableNextColumn();
                char label[32];
                std::snprintf(label, sizeof(label), "%d", i);
                bool is_selected = (selected_slave_index_ == i);
                if (ImGui::Selectable(label, is_selected,
                        ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap | ImGuiSelectableFlags_AllowDoubleClick))
                {
                    selected_slave_index_ = i;
                    if (ImGui::IsMouseDoubleClicked(0))
                    {
                        double_clicked = true;
                    }
                }

                ImGui::TableNextColumn();
                ImGui::Text("0x%04X", slave.address);

                ImGui::TableNextColumn();
                ImGui::Text("0x%08X", slave.sii.info.vendor_id);

                ImGui::TableNextColumn();
                ImGui::Text("0x%08X", slave.sii.info.product_code);

                ImGui::TableNextColumn();
                ImGui::Text("%s", resolveString(slave.sii.strings, slave.sii.general.device_name_id));
            }
            ImGui::EndTable();
        }
        return double_clicked;
    }

    void App::renderSlaveDialog()
    {
        bool is_flash = (slave_action_ == SlaveAction::Flash);
        char const* title  = is_flash ? "Flash EEPROM to Slave" : "Load EEPROM from Slave";
        char const* action = is_flash ? "Flash" : "Load";

        if (show_slave_dialog_)
        {
            ImGui::OpenPopup(title);
        }

        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_Appearing);

        if (ImGui::BeginPopupModal(title, &show_slave_dialog_))
        {
            if (is_flash)
            {
                ImGui::TextColored(COLOR_YELLOW,
                    "WARNING: This will overwrite the slave's EEPROM with the current editor content.");
                ImGui::Separator();
            }

            ImGui::Text("Select the target slave:");
            ImGui::Separator();

            bool confirmed = renderSlaveTable() and selected_slave_index_ >= 0;

            if (not device_error_.empty())
            {
                ImGui::TextColored(COLOR_RED, "%s", device_error_.c_str());
            }

            bool can_act = (selected_slave_index_ >= 0);
            if (not can_act) { ImGui::BeginDisabled(); }
            if (confirmed or ImGui::Button(action, ImVec2(120, 0)))
            {
                if (is_flash)
                {
                    flashToSlave(selected_slave_index_);
                }
                else
                {
                    loadFromSlave(selected_slave_index_);
                }
            }
            if (not can_act) { ImGui::EndDisabled(); }

            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                show_slave_dialog_ = false;
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

#ifdef __linux__
    static std::string buildSetcapCommand(std::string const& exe_path)
    {
        std::string setcap = "/usr/sbin/setcap cap_net_raw,cap_net_admin+ep " + exe_path;

        // pkexec (PolicyKit)
        if (std::system("which pkexec >/dev/null 2>&1") == 0)
        {
            return "pkexec " + setcap;
        }

        // kdesu (KDE) — search known locations
        constexpr char const* KDESU_PATHS[] = {
            "/usr/lib/x86_64-linux-gnu/libexec/kf6/kdesu",
            "/usr/lib/x86_64-linux-gnu/libexec/kf5/kdesu",
            "/usr/lib/libexec/kf6/kdesu",
            "/usr/lib/libexec/kf5/kdesu",
        };
        for (auto const* kdesu : KDESU_PATHS)
        {
            if (access(kdesu, X_OK) == 0)
            {
                return std::string(kdesu) + " -c \"" + setcap + "\"";
            }
        }

        return {};
    }
#endif

    void App::renderPrivilegeDialog()
    {
        if (needs_privilege_escalation_.load() and not isBusy())
        {
            show_privilege_dialog_ = true;
            needs_privilege_escalation_ = false;
        }

        if (show_privilege_dialog_)
        {
            ImGui::OpenPopup("Privilege Required");
        }

        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(520, 0), ImGuiCond_Always);

        if (ImGui::BeginPopupModal("Privilege Required", &show_privilege_dialog_))
        {
            ImGui::TextWrapped(
                "Raw Ethernet access requires the CAP_NET_RAW capability.\n\n"
                "Click \"Grant\" to set the capability on this executable "
                "(you will be prompted for your password by the system).");

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

#ifdef __linux__
            char exe_buf[PATH_MAX];
            ssize_t exe_len = readlink("/proc/self/exe", exe_buf, sizeof(exe_buf) - 1);
            bool have_path = (exe_len > 0);
            if (have_path)
            {
                exe_buf[exe_len] = '\0';
            }

            std::string cmd = have_path ? buildSetcapCommand(exe_buf) : std::string{};

            if (privilege_granted_)
            {
                ImGui::TextColored(COLOR_GREEN,
                    "Capability granted successfully!");
                ImGui::Spacing();
                ImGui::TextWrapped(
                    "The application needs to restart to apply the new privileges.");

                ImGui::Spacing();
                if (ImGui::Button("Restart Now", ImVec2(160, 0)))
                {
                    char* args[] = {exe_buf, nullptr};
                    execv(exe_buf, args);
                    // If execv fails, tell the user
                    privilege_error_ = "Restart failed. Please close and reopen the application.";
                    privilege_granted_ = false;
                }
            }
            else
            {
                if (cmd.empty()) { ImGui::BeginDisabled(); }
                if (ImGui::Button("Grant Capability", ImVec2(160, 0)))
                {
                    int ret = std::system(cmd.c_str());
                    if (ret == 0)
                    {
                        privilege_granted_ = true;
                    }
                    else
                    {
                        privilege_error_ = std::string("Failed to set capability. Run manually:\n"
                                                        "  sudo /usr/sbin/setcap cap_net_raw,cap_net_admin+ep ") + exe_buf;
                    }
                }
                if (cmd.empty()) { ImGui::EndDisabled(); }

                if (cmd.empty() and have_path)
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled("(no pkexec/kdesu/zenity found)");
                    ImGui::TextWrapped("Run manually:  sudo /usr/sbin/setcap cap_net_raw,cap_net_admin+ep %s", exe_buf);
                }

                if (not privilege_error_.empty())
                {
                    ImGui::TextColored(COLOR_YELLOW, "%s", privilege_error_.c_str());
                }
            }
#else
            ImGui::TextDisabled("Automatic privilege escalation is only available on Linux.");
#endif

            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0)))
            {
                show_privilege_dialog_ = false;
                privilege_granted_ = false;
                privilege_error_.clear();
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

}
