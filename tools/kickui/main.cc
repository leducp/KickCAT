#include <cfloat>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef __linux__
#include <unistd.h>
#endif

#include <portable-file-dialogs.h>

#include "imgui.h"

#include "GuiApp.h"

#include "BusProtocol.h"
#include "BusSession.h"
#include "EtherCATPanel.h"
#include "EventLog.h"
#include "MotorPanel.h"
#include "Panel.h"
#include "PdoValuesPanel.h"
#include "PrivilegeHelper.h"
#include "SdoPanel.h"
#include "Simulator.h"
#include "Theme.h"
#include "TopologyView.h"

namespace kickcat::kickui
{
    namespace
    {
        // Config for including a motor straight from the slave-list checkbox: a
        // real encoder resolution (matching the MotorPanel default) so SI
        // setpoints map to meaningful raw ticks. A bare OperateConfig{} would use
        // 1 tick/rev and scale every setpoint to ~0 (motor never moves). The user
        // refines units in the Control tab, which re-configures.
        OperateConfig defaultMotorConfig()
        {
            OperateConfig c;
            c.units.encoder_ticks_per_rev = 524288.0;
            c.units.gear_ratio            = 1.0;
            c.units.rated_torque_Nm       = 1.0;
            return c;
        }

        // pfd needs an existing directory to open in; a relative path or a bare
        // filename in a text field otherwise lands the dialog in an arbitrary
        // place. Resolve the field to the absolute folder that contains it.
        std::string dialogStartDir(char const* path)
        {
            namespace fs = std::filesystem;
            std::error_code ec;
            if ((path == nullptr) or (path[0] == '\0'))
            {
                return fs::current_path(ec).string();
            }
            fs::path p = fs::absolute(path, ec);
            if (ec)
            {
                return fs::current_path(ec).string();
            }
            if (not fs::is_directory(p, ec))
            {
                p = p.parent_path();
            }
            if (p.empty() or not fs::exists(p, ec))
            {
                return fs::current_path(ec).string();
            }
            return p.string();
        }

        // A dialog returns an absolute path; store it relative to the working
        // directory so it matches the defaults and stays portable in saved scenes.
        // A path outside the working dir just grows "../" -- the user's problem if
        // they move things. Fall back to the absolute path only if relative fails.
        std::string relativeToCwd(std::string const& absolute)
        {
            namespace fs = std::filesystem;
            std::error_code ec;
            fs::path rel = fs::relative(absolute, fs::current_path(ec), ec);
            if (ec or rel.empty())
            {
                return absolute;
            }
            return rel.generic_string();
        }
    }

    // Tabbed shell: interface + slave list on the left, panels (as tabs) for the
    // selected slave on the right.
    class Shell
    {
    public:
        Shell() = default;

        ~Shell()
        {
            // Stop the bus thread (release the Bus/Link) BEFORE the sim/tap it talks
            // to goes away, so the RT loop never cycles against a half-torn-down tap
            // during shutdown.
            session_.disconnect();
#ifdef __linux__
            stopSimulator();  // don't leave a child network_simulator running
#endif
        }

        // Bottom status bar: connection state + unacknowledged bus events (red if
        // any unacked event is severe). Its own window pinned to the viewport
        // bottom, so it stays visible regardless of the host window's scrolling.
        void renderStatusBar(float height)
        {
            ImGuiViewport const* vp = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - height));
            ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, height));
            ImGuiWindowFlags const flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize
                                         | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar
                                         | ImGuiWindowFlags_NoSavedSettings
                                         | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav;
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 4.0f));
            bool open = ImGui::Begin("##status_bar", nullptr, flags);
            ImGui::PopStyleVar();
            if (not open)
            {
                ImGui::End();
                return;
            }
            if (session_.isConnected())
            {
                ImGui::TextColored(COLOR_GREEN, "\xe2\x97\x8f %s  (%d slaves)",
                                   session_.interfaceName().c_str(),
                                   static_cast<int>(session_.devices().size()));
                if (bus_lost_)
                {
                    ImGui::SameLine();
                    ImGui::TextColored(COLOR_YELLOW, "\xe2\x80\x94 bus not responding");
                }
            }
            else if (session_.isConnecting())
            {
                ImGui::TextDisabled("\xe2\x97\x8f connecting...");
            }
            else
            {
                ImGui::TextDisabled("\xe2\x97\x8f disconnected");
            }

            size_t n = event_log_.unacked();
            if (n > 0)
            {
                ImVec4 col = COLOR_YELLOW;
                if (event_log_.unackedSevere()) { col = COLOR_RED; }
                ImGui::SameLine(0.0f, 30.0f);
                ImGui::TextColored(col, "\xe2\x9a\xa0 %zu bus event(s)", n);
                ImGui::SameLine();
                ImGui::TextDisabled("%s", event_log_.entries().back().text.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Ack"))
                {
                    event_log_.ackAll();
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Acknowledge all (details in the Diagnostics tab)");
                }
            }
            ImGui::End();
        }

        void render()
        {
            session_.update();
            if (not session_.isConnected()) { bus_lost_ = false; }  // reset across reconnects

            // Map discovered (scan-order) slaves back to editor S# so all views
            // label a given slave identically -- only when this is our sim (tap)
            // and the counts line up; otherwise fall back to the scan index.
            scan_to_editor_    = scene_.scanToEditorOrder();
            use_editor_labels_ = false;
            auto snap = session_.snapshot();
            if (session_.isConnected() and (session_.interfaceName().rfind("tap:", 0) == 0))
            {
                if (snap and (snap->slaves.size() == scan_to_editor_.size()))
                {
                    use_editor_labels_ = true;
                }
            }
            event_log_.update(snap, session_.isConnected(), bus_lost_,
                              [this](int i) { return slaveLabel(i); });

            renderMenuBar();

            // Resizable two-pane split (drag the divider to widen the sidebar).
            // Fill the window height minus the room kept for the status bar, else
            // the panes collapse to content size.
            float status_h = ImGui::GetFrameHeight() + 8.0f;   // matches the bar's 4px paddings
            float body_h   = ImGui::GetContentRegionAvail().y - status_h;
            if (body_h < 1.0f)
            {
                body_h = 1.0f;
            }
            if (ImGui::BeginTable("##layout", 2,
                    ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV,
                    ImVec2(0.0f, body_h)))
            {
                ImGui::TableSetupColumn("##side", ImGuiTableColumnFlags_WidthFixed, px(300.0f));
                ImGui::TableSetupColumn("##main", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::BeginChild("##sidebar", ImVec2(0.0f, 0.0f), false);
                renderSidebar();
                ImGui::EndChild();

                ImGui::TableSetColumnIndex(1);
                ImGui::BeginChild("##content", ImVec2(0.0f, 0.0f), false);
                renderContent();
                ImGui::EndChild();

                ImGui::EndTable();
            }

            renderStatusBar(status_h);
            renderPrivilegeDialog();
        }

    private:
        // Connection state lives in the status bar; the menu bar is the title.
        void renderMenuBar()
        {
            if (not ImGui::BeginMenuBar())
            {
                return;
            }
            ImGui::TextUnformatted("KickUI");
            ImGui::EndMenuBar();
        }

#ifdef __linux__
        void launchSimulator()
        {
            sim_error_.clear();
            // The launch unlinks the tap shm, which would desync a live session
            // sharing it.
            if (session_.isConnected() or session_.isConnecting())
            {
                sim_error_ = "disconnect before launching a simulator";
                return;
            }
            if (not sim_proc_.launch(scene_, sim_error_))
            {
                return;
            }
            // Point the connection selector at the simulator so the next click is
            // just "Connect" (instead of leaving it on the first NIC, e.g. lo).
            auto const& ifs = session_.interfaces();
            for (int i = 0; i < static_cast<int>(ifs.size()); ++i)
            {
                if (ifs[i].name == "tap:client") { iface_index_ = i; break; }
            }
        }

        void stopSimulator()
        {
            if (not sim_proc_.running()) { return; }
            // Release the bus before killing the tap peer (same reason as ~Shell):
            // a live session on this sim must not keep cycling once the tap is gone.
            if (session_.isConnected() or session_.isConnecting())
            {
                session_.disconnect();
            }
            sim_proc_.stop();
            onSimGone();
        }

        void reapSimulator()
        {
            if (sim_proc_.reap())
            {
                sim_error_ = "simulator exited (check the binary/config path)";
                onSimGone();
            }
        }

        // Drain the simulator's return stream once per frame and dispatch each
        // message on its type. Runs even when the panel is collapsed so the ring
        // never backs up (which would otherwise stall command acks).
        void pollSimulator()
        {
            sim::ControlEvent ev;
            while (sim_proc_.nextEvent(ev))
            {
                if (ev.type == sim::ControlEvent::Type::FrameStats)
                {
                    sim_last_stats_ = ev.payload.stats;
                    sim_has_stats_  = true;
                    sim_avg_history_.push_back(static_cast<float>(ev.payload.stats.avg_ns) / 1000.0f);
                    constexpr size_t MAX_SAMPLES = 240;
                    if (sim_avg_history_.size() > MAX_SAMPLES)
                    {
                        sim_avg_history_.erase(sim_avg_history_.begin(),
                            sim_avg_history_.end() - MAX_SAMPLES);
                    }
                }
                // SetLinkAck: draining it is what matters (keeps the ring clear);
                // the editor already tracks broken links optimistically.
            }
        }

        // Live frame-timing of the running simulator: min/max/avg of the last
        // window plus a sparkline of the per-window average over time.
        void renderSimStats()
        {
            if (not sim_has_stats_)
            {
                ImGui::TextDisabled("frame timing: waiting for first window...");
                return;
            }

            double const min_us = sim_last_stats_.min_ns / 1000.0;
            double const max_us = sim_last_stats_.max_ns / 1000.0;
            double const avg_us = sim_last_stats_.avg_ns / 1000.0;
            ImGui::Text("frame timing (n=%llu)", static_cast<unsigned long long>(sim_last_stats_.window));
            ImGui::Text("  min %.1f  max %.1f  avg %.1f \xc2\xb5s  (jitter \xc2\xb1%.1f)",
                        min_us, max_us, avg_us, (max_us - min_us) / 2.0);

            if (not sim_avg_history_.empty())
            {
                char overlay[32];
                std::snprintf(overlay, sizeof(overlay), "avg %.0f \xc2\xb5s", avg_us);
                ImGui::PlotLines("##sim_avg", sim_avg_history_.data(),
                                 static_cast<int>(sim_avg_history_.size()),
                                 0, overlay, FLT_MAX, FLT_MAX, ImVec2(px(220.0f), px(40.0f)));
            }
        }

        void renderSimulator()
        {
            reapSimulator();
            pollSimulator();
            if (not ImGui::CollapsingHeader("Simulator (no hardware)")) { return; }
            ImGui::TextDisabled("Spawns network_simulator on tap:server;\nthen connect with interface tap:client.");

            bool running = sim_proc_.running();
            // Frozen only while the sim is running (you can't reconfigure a live
            // sim). Once stopped, the config is editable again even if a stale
            // session is still up -- launchSimulator() refuses while connected.
            ImGui::BeginDisabled(running);

            // One row per simulated slave: config + its parent. Just pick a parent;
            // the downstream port on that parent is assigned automatically, so
            // branches never collide. The assigned port is shown read-only. S0 is
            // the root, wired to the master.
            ImGui::TextDisabled("Pick each slave's parent; branch ports are auto-assigned.");
            std::vector<int> ports = scene_.assignedPorts();
            for (int i = 0; i < static_cast<int>(scene_.slaves.size()); ++i)
            {
                ImGui::PushID(i);
                SimSlave& s = scene_.slaves[i];
                ImGui::AlignTextToFramePadding();
                ImGui::Text("S%d", i);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(px(150.0f));
                ImGui::InputText("##cfg", s.config, sizeof(s.config));
                ImGui::SameLine();
                if (ImGui::Button("..."))
                {
                    auto sel = pfd::open_file("Select slave config", dialogStartDir(s.config),
                                              {"Slave config", "*.json", "All files", "*"}).result();
                    if (not sel.empty())
                    {
                        std::string chosen = relativeToCwd(sel[0]);
                        std::snprintf(s.config, sizeof(s.config), "%s", chosen.c_str());
                    }
                }

                ImGui::SameLine();
                ImGui::SetNextItemWidth(px(80.0f));
                std::string parent_label = "master";
                if (s.parent >= 0) { parent_label = "S" + std::to_string(s.parent); }
                if (ImGui::BeginCombo("##parent", parent_label.c_str()))
                {
                    if (ImGui::Selectable("master", s.parent == -1)) { s.parent = -1; }
                    for (int p = 0; p < i; ++p)   // only earlier slaves: keeps the tree acyclic
                    {
                        std::string lbl = "S" + std::to_string(p);
                        if (ImGui::Selectable(lbl.c_str(), s.parent == p)) { s.parent = p; }
                    }
                    ImGui::EndCombo();
                }
                if (s.parent >= 0)
                {
                    ImGui::SameLine();
                    if (ports[i] >= 0)
                    {
                        ImGui::TextDisabled("\xe2\x86\x92 S%d port %d", s.parent, ports[i]);
                    }
                    else
                    {
                        ImGui::TextColored(COLOR_RED, "\xe2\x86\x92 S%d (no free port!)", s.parent);
                    }
                }
                ImGui::PopID();
            }

            if (ImGui::Button("Add slave"))
            {
                SimSlave s;
                s.parent = static_cast<int>(scene_.slaves.size()) - 1;  // default: chain off the tail
                scene_.slaves.push_back(s);
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(scene_.slaves.size() <= 1);
            if (ImGui::Button("Remove slave"))
            {
                scene_.slaves.pop_back();
                for (SimSlave& s : scene_.slaves)
                {
                    if (s.parent >= static_cast<int>(scene_.slaves.size())) { s.parent = -1; }
                }
            }
            ImGui::EndDisabled();

            ImGui::Checkbox("Cable redundancy (ring)", &scene_.redundancy);
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Close the ring on the tail slave and run a redundant tap pair.\n"
                                  "Connect via tap:client to use the redundant path too.");
            }

            ImGui::EndDisabled();

            // Save/load the editor scene. Save is read-only, so it stays enabled
            // while the sim runs (snapshot the live config); Load would desync the
            // editor from the running sim, so it is gated like the rest.
            // The text field holds the path; "Browse..." fills it via a dialog,
            // and Save/Load act on whatever it holds.
            ImGui::SetNextItemWidth(px(170.0f));
            ImGui::InputText("scene", sim_scene_path_, sizeof(sim_scene_path_));
            ImGui::SameLine();
            if (ImGui::Button("...##scene"))
            {
                auto sel = pfd::open_file("Select simulator scene", dialogStartDir(sim_scene_path_),
                                          {"Scene files", "*.txt", "All files", "*"}).result();
                if (not sel.empty())
                {
                    std::string chosen = relativeToCwd(sel[0]);
                    std::snprintf(sim_scene_path_, sizeof(sim_scene_path_), "%s", chosen.c_str());
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Save")) { scene_.save(sim_scene_path_, sim_scene_msg_); }
            ImGui::SameLine();
            ImGui::BeginDisabled(running or session_.isConnected() or session_.isConnecting());
            if (ImGui::Button("Load")) { scene_.load(sim_scene_path_, sim_scene_msg_); }
            ImGui::EndDisabled();
            if (not sim_scene_msg_.empty())
            {
                ImGui::TextDisabled("%s", sim_scene_msg_.c_str());
            }

            if (running)
            {
                ImGui::TextColored(COLOR_GREEN, "\xe2\x97\x8f simulator running (pid %d)", sim_proc_.pid());
                renderSimStats();
                if (ImGui::Button("Stop simulator")) { stopSimulator(); }
            }
            else
            {
                if (ImGui::Button("Launch simulator")) { launchSimulator(); }
            }
            if (not sim_error_.empty())
            {
                ImGui::TextColored(COLOR_YELLOW, "%s", sim_error_.c_str());
            }
        }
#endif

        // The simulator just died (stopped or crashed). The tap is shared memory,
        // so the master sees no socket error -- its frames simply stop being
        // answered, and per-slave state goes stale. Flag it so the slaves render
        // greyed (state unknown) while the link stays up, rather than silently
        // showing a stale state as if it were live.
        void onSimGone()
        {
            sim_has_stats_ = false;
            sim_avg_history_.clear();
            if (session_.isConnected() and (session_.interfaceName().rfind("tap:", 0) == 0))
            {
                bus_lost_ = true;
            }
        }

        ImVec4 slaveStateColor(int slave_index)
        {
            return esmColor(session_.slaveAlStatus(slave_index), bus_lost_);
        }

        void renderSidebar()
        {
            // Reserve a fixed footer so the operation controls (Apply mapping /
            // Back to PRE-OP) stay reachable no matter how long the slave list
            // grows on a short window.
            float footer_h = 0.0f;
            if (session_.isConnected())
            {
                footer_h = ImGui::GetFrameHeightWithSpacing() * 3.0f;
            }
            ImGui::BeginChild("##sidebar_scroll", ImVec2(0.0f, -footer_h), false);

            ImGui::SeparatorText("Connection");
#ifdef __linux__
            renderSimulator();
#endif

            // The simulator pseudo-interface "tap:client" shows its friendly
            // description instead of the raw connection string. Probed interfaces
            // carry their detection outcome.
            auto ifaceLabel = [this](NetworkInterface const& nif) -> std::string
            {
                std::string label;
                if (nif.name == "tap:client")
                {
                    label = nif.description;
                }
                else
                {
                    label = nif.name + "  (" + nif.description + ")";
                }
                int found = session_.detectResult(nif.name);
                if (found > 0)
                {
                    char const* plural = "s";
                    if (found == 1)
                    {
                        plural = "";
                    }
                    label += "  \xe2\x80\x94 EtherCAT: " + std::to_string(found) + " slave" + plural;
                }
                else if (found == 0)
                {
                    label += "  \xe2\x80\x94 no EtherCAT";
                }
                return label;
            };

            auto const& ifaces = session_.interfaces();
            std::string preview = "(no interface)";
            if ((iface_index_ >= 0) and (iface_index_ < static_cast<int>(ifaces.size())))
            {
                preview = ifaceLabel(ifaces[iface_index_]);
            }

            ImGui::SetNextItemWidth(-1.0f);
            if (ImGui::BeginCombo("##iface", preview.c_str()))
            {
                for (int i = 0; i < static_cast<int>(ifaces.size()); ++i)
                {
                    bool selected = (i == iface_index_);
                    if (ImGui::Selectable(ifaceLabel(ifaces[i]).c_str(), selected))
                    {
                        iface_index_ = i;
                    }
                    if (selected)
                    {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            ImGui::BeginDisabled(session_.isConnected() or session_.isConnecting());
            ImGui::SetNextItemWidth(px(90.0f));
            ImGui::InputInt("Bus cycle (ms)", &cycle_ms_);
            if (cycle_ms_ < 1)
            {
                cycle_ms_ = 1;
            }
            ImGui::Checkbox("DC sync (SYNC0)", &dc_enable_);
            ImGui::EndDisabled();

            bool has_iface = (iface_index_ >= 0) and (iface_index_ < static_cast<int>(ifaces.size()));
            bool can_connect = has_iface and not session_.isConnecting() and not session_.isConnected();

            ImGui::BeginDisabled(not can_connect);
            if (ImGui::Button("Connect"))
            {
                session_.setDcConfig(dc_enable_, cycle_ms_);
                // A redundant simulator (ring) exposes its second port on the
                // default tap_redundant pair: connect the master to it too.
                std::string redundant = "";
                if (scene_.redundancy and (ifaces[iface_index_].name == "tap:client"))
                {
                    redundant = "tap:client";
                }
                session_.setRedundancy(redundant);
                session_.connect(ifaces[iface_index_].name);
            }
            ImGui::EndDisabled();

            ImGui::SameLine();
            ImGui::BeginDisabled(not session_.isConnected());
            if (ImGui::Button("Disconnect"))
            {
                session_.disconnect();
            }
            ImGui::EndDisabled();

            ImGui::SameLine();
            ImGui::BeginDisabled(session_.isConnecting());
            if (ImGui::Button("Refresh"))
            {
                session_.refreshInterfaces();
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Re-list available network interfaces");
            }

            ImGui::SameLine();
            ImGui::BeginDisabled(session_.isConnected() or session_.isConnecting() or session_.isDetecting());
            if (ImGui::Button("Detect"))
            {
                session_.detectNetworks();
                detect_was_running_ = true;
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Probe each interface for an EtherCAT network\n(broadcast read; the answer count is the number of slaves)");
            }

            if (session_.isDetecting())
            {
                ImGui::TextDisabled("\xe2\x97\x8f detecting...");
            }
            else
            {
                if (detect_was_running_)
                {
                    // Detection just finished: pre-select the first interface where
                    // an EtherCAT network answered (unless one is already chosen).
                    detect_was_running_ = false;
                    bool keep = (iface_index_ >= 0) and (iface_index_ < static_cast<int>(ifaces.size()))
                            and (session_.detectResult(ifaces[iface_index_].name) > 0);
                    if (not keep)
                    {
                        for (int i = 0; i < static_cast<int>(ifaces.size()); ++i)
                        {
                            if (session_.detectResult(ifaces[i].name) > 0)
                            {
                                iface_index_ = i;
                                break;
                            }
                        }
                    }
                }
                std::string detect_status = session_.detectStatus();
                if (not detect_status.empty())
                {
                    ImGui::TextDisabled("%s", detect_status.c_str());
                }
            }

            if (session_.isConnected())
            {
                ImGui::TextColored(COLOR_GREEN, "\xe2\x97\x8f connected");
                if (bus_lost_)
                {
                    ImGui::TextColored(COLOR_YELLOW, "bus not responding (simulator stopped?)");
                }
            }
            else if (session_.isConnecting())
            {
                ImGui::TextDisabled("\xe2\x97\x8f connecting...");
            }
            else
            {
                ImGui::TextDisabled("\xe2\x97\x8f disconnected");
            }

            std::string status = session_.status();
            if (not status.empty())
            {
                ImGui::TextWrapped("%s", status.c_str());
            }
            std::string conn_error = session_.error();
            if (not conn_error.empty())
            {
                ImGui::PushStyleColor(ImGuiCol_Text, COLOR_RED);
                ImGui::TextWrapped("%s", conn_error.c_str());
                ImGui::PopStyleColor();
            }

            ImGui::SeparatorText("Slaves");
            ImGui::BeginDisabled(not session_.isConnected() or session_.isConnecting());
            if (ImGui::Button("Rescan bus"))
            {
                session_.rescan();
            }
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Re-detect slaves on the current interface");
            }
            renderSlaveList();

            ImGui::EndChild();

            // Pinned footer: always visible, outside the scrolling region above.
            if (session_.isConnected())
            {
                renderOperationControls();
            }
        }

        // Bus-level operation: the PDO mapping is built ONCE (at PRE-OP) over every
        // drive marked "Include in mapping", then the cyclic loop runs continuously.
        // To change the mapped set, drop back to PRE-OP and re-apply.
        void renderOperationControls()
        {
            ImGui::SeparatorText("Operation");
            bool operating = session_.isOperatingAny();
            if (operating)
            {
                ImGui::TextColored(COLOR_GREEN, "\xe2\x97\x8f operating (mapping applied)");
                if (ImGui::Button("Back to PRE-OP (all)"))
                {
                    session_.backToPreOp();
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Stop the cyclic loop and return the whole bus to PRE-OP\n"
                                      "so the mapped set can be changed and re-applied.");
                }
            }
            else
            {
                ImGui::TextDisabled("PRE-OP \xe2\x80\x94 include drives, then apply the mapping");
                if (ImGui::Button("Apply mapping"))
                {
                    session_.applyMapping();
                }
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Map every included drive at once and start the cyclic loop.\n"
                                      "Each drive is then brought to SAFE-OP/OP independently.");
                }
            }
        }

        void renderSlaveList()
        {
            auto& slaves = session_.devices();
            if (slaves.empty())
            {
                ImGui::TextDisabled("No slaves.");
                return;
            }

            // Fixed columns keep the state/profile aligned across rows (varying
            // state-name length no longer shifts them). The leading checkbox is
            // "include in the PDO mapping" (disabled once the loop is running).
            bool operating = session_.isOperatingAny();
            if (not ImGui::BeginTable("##slaves", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit))
            {
                return;
            }
            ImGui::TableSetupColumn("##map",     ImGuiTableColumnFlags_WidthFixed, px(22.0f));
            ImGui::TableSetupColumn("##name",    ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("##state",   ImGuiTableColumnFlags_WidthFixed, px(62.0f));
            ImGui::TableSetupColumn("##profile", ImGuiTableColumnFlags_WidthFixed, px(86.0f));

            for (int i = 0; i < static_cast<int>(slaves.size()); ++i)
            {
                Device& slave = slaves[i];
                ImGui::TableNextRow();
                ImGui::PushID(i);

                ImGui::TableSetColumnIndex(0);
                if (slave.has_coe or slave.sii_pdo)
                {
                    bool mapped = slave.isConfigured();
                    ImGui::BeginDisabled(operating);
                    if (ImGui::Checkbox("##map", &mapped))
                    {
                        if (not mapped)              { slave.unconfigureSlave(); }
                        else if (slave.isMotor())    { slave.configureSlave(defaultMotorConfig()); }
                        else                         { slave.includeSlave(); }
                    }
                    ImGui::EndDisabled();
                    if (ImGui::IsItemHovered()) { ImGui::SetTooltip("Include in the PDO mapping"); }
                }

                ImGui::TableSetColumnIndex(1);
                char label[128];
                std::snprintf(label, sizeof(label), "S%d  %s", slaveLabel(i), slave.name.c_str());
                if (ImGui::Selectable(label, i == session_.selected()))
                {
                    session_.select(i);
                }

                ImGui::TableSetColumnIndex(2);
                uint8_t al = session_.slaveAlStatus(i);
                ImGui::TextColored(slaveStateColor(i), "%s", stateLabel(al));

                ImGui::TableSetColumnIndex(3);
                if (slave.isMotor())
                {
                    ImGui::TextColored(COLOR_DS402, "DS402");
                }
                if (slave.is_emulated)
                {
                    if (slave.isMotor())
                    {
                        ImGui::SameLine();
                    }
                    ImGui::TextDisabled("emu");
                    if (ImGui::IsItemHovered())
                    {
                        ImGui::SetTooltip("Device emulation (no PDI application):\n"
                                          "AL status mirrors AL control, the error bit is\n"
                                          "the echoed ack request and is filtered out.");
                    }
                }

                ImGui::PopID();
            }
            ImGui::EndTable();
        }

        void renderContent()
        {
            if (not session_.isConnected())
            {
                std::string error = session_.error();
                if (not error.empty())
                {
                    ImGui::TextColored(COLOR_RED, "Connection failed");
                    ImGui::Separator();
                    ImGui::TextWrapped("%s", error.c_str());
                }
                else
                {
                    ImGui::TextDisabled("Select a network interface and connect to a bus.");
                }
                device_panels_.clear();  // drop per-device panels; indices change on reconnect/rescan
                return;
            }

            // Top-level tabs: the bus-wide topology (master POV) vs the detail of
            // the selected slave. The topology tab is independent of the selection.
            if (ImGui::BeginTabBar("##content_tabs"))
            {
                if (ImGui::BeginTabItem("Bus Topology"))
                {
                    TopologyView::SimHooks hooks;
#ifdef __linux__
                    hooks.running         = sim_proc_.running();
                    hooks.broken_links    = &sim_proc_.brokenLinks();
                    hooks.set_link_broken = [this](int a, int b, bool broken)
                    {
                        sim_proc_.setLinkBroken(a, b, broken);
                    };
#endif
                    topo_view_.render(session_, scene_,
                                      [this](int i) { return slaveLabel(i); }, bus_lost_, hooks);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Device"))
                {
                    renderDeviceDetail();
                    ImGui::EndTabItem();
                }
                // The "###diag" suffix fixes the tab's ImGui ID, so the visible count
                // can change (or the banner ack fire) WITHOUT the tab losing selection.
                char diag_label[40] = "Diagnostics###diag";
                if (event_log_.unacked() > 0)
                {
                    std::snprintf(diag_label, sizeof(diag_label), "Diagnostics (%zu)###diag",
                                  event_log_.unacked());
                }
                if (ImGui::BeginTabItem(diag_label))
                {
                    renderDiagnostics(session_.snapshot());
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }

        void renderDeviceDetail()
        {
            Device* slave = session_.selectedDevice();
            if (slave == nullptr)
            {
                ImGui::TextDisabled("Select a slave.");
                return;
            }

            ImGui::Text("%s", slave->name.c_str());
            ImGui::SameLine();
            if (slave->isMotor())
            {
                ImGui::TextColored(COLOR_DS402, "[DS402]");
                if (slave->is_emulated)
                {
                    ImGui::SameLine();
                }
            }
            if (slave->is_emulated)
            {
                ImGui::TextDisabled("[emulated]");
                if (ImGui::IsItemHovered())
                {
                    ImGui::SetTooltip("Device emulation (ESC config 0x141 bit 0): AL status\n"
                                      "mirrors AL control, so the AL error bit only echoes the\n"
                                      "master's ack request. It is filtered from the state display.");
                }
            }
            ImGui::TextDisabled("S%d  @%u  vendor 0x%08X  product 0x%08X",
                                slaveLabel(slave->index), slave->address, slave->vendor_id, slave->product_code);

            if (slave->has_coe)
            {
                // Override combo, built from the registry: slot 0 is "Auto" (follow
                // detection), then one entry per selectable profile.
                auto const& profiles = selectableProfiles();
                std::vector<char const*> labels{"Auto"};
                int current = 0;
                for (size_t i = 0; i < profiles.size(); ++i)
                {
                    labels.push_back(profiles[i].name);
                    if (profiles[i].profile == slave->forced_profile) { current = static_cast<int>(i + 1); }
                }
                ImGui::SetNextItemWidth(px(150.0f));
                if (ImGui::Combo("Profile", &current, labels.data(), static_cast<int>(labels.size())))
                {
                    if (current == 0) { slave->forceProfile(Profile::Unknown); }
                    else              { slave->forceProfile(profiles[current - 1].profile); }
                }
                ImGui::SameLine();
                ImGui::TextDisabled("(detected: %s)", profileInfo(slave->detected_profile).name);
            }

            // EtherCAT FSM state: slave-level, common to all tabs.
            ImGui::Separator();
            uint8_t al = session_.slaveAlStatus(slave->index);
            ImGui::TextUnformatted("EtherCAT:");
            ImGui::SameLine();
            if (al & AL_ERROR_BIT)
            {
                ImGui::TextColored(slaveStateColor(slave->index), "%s (error)", stateLabel(al));
            }
            else
            {
                ImGui::TextColored(slaveStateColor(slave->index), "%s", stateLabel(al));
            }
            for (auto const& btn : STATE_BUTTONS)
            {
                ImGui::SameLine();
                if (ImGui::SmallButton(btn.label))
                {
                    slave->requestState(static_cast<uint8_t>(btn.state));
                }
            }
            std::string state_error = session_.stateActionError(slave->index);
            if (not state_error.empty())
            {
                ImGui::TextColored(COLOR_RED, "%s", state_error.c_str());
            }

            ImGui::Separator();

            // Scope panel widget IDs by slave so per-widget ImGui state (text
            // edits, table scroll) does not bleed across slave selection.
            ImGui::PushID(slave->index);
            int applicable = 0;
            if (ImGui::BeginTabBar("##panels"))
            {
                for (auto& panel : panelsFor(slave->index))
                {
                    if (not panel->appliesTo(*slave))
                    {
                        continue;
                    }
                    ++applicable;
                    if (ImGui::BeginTabItem(panel->title()))
                    {
                        panel->render(session_, *slave);
                        ImGui::EndTabItem();
                    }
                }
                ImGui::EndTabBar();
            }
            ImGui::PopID();

            if (applicable == 0)
            {
                ImGui::TextDisabled("No applicable panels for this slave "
                                    "(no CoE mailbox / not a DS402 drive).");
            }
        }

        void renderDiagnostics(std::shared_ptr<const BusSnapshot> const& snap)
        {
            if (session_.redundancyEnabled())
            {
                renderRedundancyStatus(snap and snap->redundancy_active);
            }
            if (bus_lost_)
            {
                ImGui::TextColored(COLOR_RED, "Bus communication lost (simulator/cable down)");
            }

            if (ImGui::SmallButton("Reset error counters"))
            {
                session_.clearErrorCounters();   // slaves + the GUI totals
            }
            ImGui::SameLine();
            ImGui::TextDisabled("totals auto-accumulate; slave counters auto-clear before saturation");

            ImGui::SeparatorText("Per-slave");
            if (snap and not snap->slaves.empty()
                and ImGui::BeginTable("##diag", 3,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("Slave / AL state", ImGuiTableColumnFlags_WidthFixed, px(220.0f));
                ImGui::TableSetupColumn("Ports  link / lost / rxerr");
                ImGui::TableSetupColumn("AL status code", ImGuiTableColumnFlags_WidthFixed, px(230.0f));
                ImGui::TableHeadersRow();
                for (int i = 0; i < static_cast<int>(snap->slaves.size()); ++i)
                {
                    SlaveSnapshot const& s = snap->slaves[i];
                    bool err = (s.al_status & AL_ERROR_BIT) != 0;

                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImVec4 sc = esmColor(s.al_status);
                    char const* suffix = "";
                    if (err) { suffix = "+ERR"; }
                    ImGui::TextColored(sc, "S%d  %s%s", slaveLabel(i), stateLabel(s.al_status), suffix);

                    ImGui::TableNextColumn();
                    for (int p = 0; p < 4; ++p)
                    {
                        char const* link = "down";
                        ImVec4 lc = COLOR_RED;
                        if (s.port_com[p]) { link = "up"; lc = COLOR_GREEN; }
                        if (p > 0) { ImGui::SameLine(); }
                        ImGui::TextColored(lc, "p%d:%s", p, link);
                        ImGui::SameLine();
                        // Totals since the last reset.
                        ImVec4 tc = ImVec4(0.70f, 0.72f, 0.76f, 1.0f);
                        if ((s.stats.lost_total[p] > 0) or (s.stats.rxerr_total[p] > 0)) { tc = COLOR_YELLOW; }
                        ImGui::TextColored(tc, "lost %llu rx %llu",
                                           static_cast<unsigned long long>(s.stats.lost_total[p]),
                                           static_cast<unsigned long long>(s.stats.rxerr_total[p]));
                    }
                    if (s.stats.saturated)
                    {
                        ImGui::SameLine();
                        ImGui::TextColored(COLOR_RED, "(saturated!)");
                    }

                    ImGui::TableNextColumn();
                    if (err)
                    {
                        ImGui::TextColored(COLOR_RED, "0x%04X %s", s.stats.al_status_code,
                                           ALStatus_to_string(s.stats.al_status_code));
                    }
                    else
                    {
                        ImGui::TextDisabled("\xe2\x80\x94");
                    }
                }
                ImGui::EndTable();
            }

            ImGui::SeparatorText("Event log");
            if (ImGui::SmallButton("Acknowledge all")) { event_log_.ackAll(); }
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear log"))       { event_log_.clear(); }
            ImGui::SameLine();
            ImGui::TextDisabled("%zu entries", event_log_.entries().size());

            ImGui::BeginChild("##log", ImVec2(0.0f, 0.0f), ImGuiChildFlags_Borders);
            auto const& entries = event_log_.entries();
            for (auto it = entries.rbegin(); it != entries.rend(); ++it)   // newest first
            {
                ImVec4 c = ImVec4(0.70f, 0.72f, 0.76f, 1.0f);
                if (it->severe) { c = COLOR_RED; }
                ImGui::TextDisabled("%s", it->when.c_str());
                ImGui::SameLine();
                ImGui::TextColored(c, "%s", it->text.c_str());
            }
            if (entries.empty())
            {
                ImGui::TextDisabled("no events yet");
            }
            ImGui::EndChild();
        }

        // The label to show for a discovered slave at scan index `i`: its editor S#
        // when we can map it, else the scan index itself.
        int slaveLabel(int scan_index) const
        {
            if (use_editor_labels_ and (scan_index >= 0)
                and (scan_index < static_cast<int>(scan_to_editor_.size())))
            {
                return scan_to_editor_[scan_index];
            }
            return scan_index;
        }

        void renderPrivilegeDialog()
        {
            priv_.reap();
            if (session_.needsPrivilege() and not session_.isConnecting())
            {
                show_privilege_dialog_ = true;
                session_.clearNeedsPrivilege();
            }
            if (show_privilege_dialog_)
            {
                ImGui::OpenPopup("Privilege Required");
                show_privilege_dialog_ = false;
            }

            ImVec2 center = ImGui::GetMainViewport()->GetCenter();
            ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
            ImGui::SetNextWindowSize(ImVec2(px(520.0f), 0.0f), ImGuiCond_Always);

            if (not ImGui::BeginPopupModal("Privilege Required"))
            {
                return;
            }

            ImGui::TextWrapped(
                "Raw Ethernet access requires the CAP_NET_RAW capability.\n\n"
                "Click \"Grant\" to set the capability on this executable "
                "(the system will prompt for your password).");
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

#ifdef __linux__
            priv_.ensureCommand();
            bool have_path = not priv_.exePath().empty();

            if (priv_.granted())
            {
                ImGui::TextColored(COLOR_GREEN, "Capability granted. Restart to apply.");
                ImGui::Spacing();
                if (have_path)
                {
                    if (ImGui::Button("Restart Now", ImVec2(px(160.0f), 0.0f)))
                    {
                        // execv skips destructors: stop the bus and the child sim
                        // first, else the sim is orphaned holding its tap shm.
                        session_.disconnect();
                        stopSimulator();
                        std::vector<char> exe(priv_.exePath().begin(), priv_.exePath().end());
                        exe.push_back('\0');
                        char* args[] = {exe.data(), nullptr};
                        execv(exe.data(), args);
                        priv_.setError("Restart failed. Please close and reopen KickUI.");
                        priv_.resetGranted();
                    }
                }
                else
                {
                    ImGui::TextDisabled("Restart KickUI manually to apply.");
                }
            }
            else
            {
                if (priv_.running())
                {
                    ImGui::TextColored(COLOR_YELLOW, "Granting (answer the password prompt)...");
                }
                ImGui::BeginDisabled(priv_.command().empty() or priv_.running());
                if (ImGui::Button("Grant Capability", ImVec2(px(160.0f), 0.0f)))
                {
                    priv_.grant();
                }
                ImGui::EndDisabled();

                if (priv_.command().empty() and have_path)
                {
                    ImGui::TextWrapped("No pkexec/kdesu found. Run manually:\n"
                        "  sudo /usr/sbin/setcap cap_net_raw,cap_net_admin+ep %s", priv_.exePath().c_str());
                }
                if (not priv_.error().empty())
                {
                    ImGui::TextColored(COLOR_YELLOW, "%s", priv_.error().c_str());
                }
            }
#else
            ImGui::TextDisabled("Automatic privilege escalation is only available on Linux.");
#endif

            ImGui::SameLine();
            ImGui::BeginDisabled(priv_.running());
            if (ImGui::Button("Cancel", ImVec2(px(120.0f), 0.0f)))
            {
                priv_.resetGranted();
                priv_.clearError();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();

            ImGui::EndPopup();
        }

        // The set of feature panels for one device. Built per device so each
        // device gets its OWN panel instances -- no widget state bleeds across
        // slaves. Adding a feature (ESM, error counters, EoE/FoE/SoE, ...) = one
        // push_back here + a Panel subclass that appliesTo the right capability.
        static std::vector<std::unique_ptr<Panel>> makePanels()
        {
            std::vector<std::unique_ptr<Panel>> panels;
            panels.push_back(std::make_unique<MotorPanel>());
            panels.push_back(std::make_unique<EtherCATPanel>());
            panels.push_back(std::make_unique<PdoValuesPanel>());
            panels.push_back(std::make_unique<SdoPanel>());
            return panels;
        }

        std::vector<std::unique_ptr<Panel>>& panelsFor(int device_index)
        {
            auto it = device_panels_.find(device_index);
            if (it == device_panels_.end())
            {
                it = device_panels_.emplace(device_index, makePanels()).first;
            }
            return it->second;
        }

        BusSession session_;
        std::unordered_map<int, std::vector<std::unique_ptr<Panel>>> device_panels_;
        int  iface_index_ = 0;
        bool dc_enable_   = false;
        int  cycle_ms_    = 1;
        bool detect_was_running_ = false;   // edge: auto-select once when a probe ends

        bool            show_privilege_dialog_ = false;
        PrivilegeHelper priv_;

        // The simulator: editor scene (all platforms) + child process (Linux).
        SimScene    scene_;
        std::string sim_error_;
        char        sim_scene_path_[256] = "sim_scene.txt";  // save/load the editor scene
        std::string sim_scene_msg_;
#ifdef __linux__
        SimulatorProcess sim_proc_;
        sim::SimStats      sim_last_stats_{};       // most recent window drained
        bool               sim_has_stats_ = false;
        std::vector<float> sim_avg_history_;        // per-window avg (µs), for the sparkline
#endif

        EventLog     event_log_;
        TopologyView topo_view_;
        bool         bus_lost_ = false;  // sim died: slaves stale, render greyed

        // Coherent slave labels: the master discovers slaves in EtherCAT scan
        // (processing) order, which differs from the editor's add order. When we
        // launched the sim from the editor we map each scan position back to its
        // editor S# so every view (slave list, device, topology) shows the SAME
        // label for a given slave. Empty / disabled => fall back to the scan index.
        std::vector<int> scan_to_editor_;
        bool             use_editor_labels_ = false;
    };
}

int main(int, char**)
{
    kickcat::gui::GuiApp gui{"KickUI"};
    if (not gui.valid())
    {
        return 1;
    }
    kickcat::kickui::g_mono_font = gui.monoFont();
    kickcat::kickui::g_ui_scale  = gui.scale();

    kickcat::kickui::Shell shell;
    gui.run([&shell]{ shell.render(); });

    return 0;
}
