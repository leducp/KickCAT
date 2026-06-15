#include "MotorPanel.h"

#include <cstdlib>

#include "imgui.h"

#include "kickcat/CoE/CiA/DS402/Drive.h"
#include "kickcat/CoE/CiA/DS402/StateMachine.h"

#include "BusSession.h"
#include "BusProtocol.h"
#include "Theme.h"

namespace kickcat::kickui
{
    namespace
    {
        namespace ctrl   = CoE::CiA::DS402::control;
        namespace status = CoE::CiA::DS402::status;

        char const* const MODE_LABELS[] = {"CSP — cyclic position",
                                            "CSV — cyclic velocity",
                                            "CST — cyclic torque"};
        ctrl::ControlMode const MODE_VALUES[] = {ctrl::POSITION_CYCLIC,
                                                 ctrl::VELOCITY_CYCLIC,
                                                 ctrl::TORQUE_CYCLIC};
        char const* const WAVE_LABELS[] = {"Sine", "Step", "Triangle"};

        // Unit of the active setpoint quantity, by mode index.
        char const* setpointUnit(int mode_index)
        {
            if (mode_index == 1) { return "rad/s"; }
            if (mode_index == 2) { return "Nm"; }
            return "rad";
        }
    }

    bool MotorPanel::appliesTo(Device const& device) const
    {
        return device.isMotor();
    }

    float MotorPanel::safeManualTarget(DriveFeedback const& fb) const
    {
        if (mode_index_ == 0)  // CSP: hold the current position
        {
            return static_cast<float>(fb.actual_pos);
        }
        return 0.0f;  // velocity / torque: a stop is zero, not the position value
    }

    void MotorPanel::render(BusSession& session, Device& device)
    {
        bool operating_this = device.isOperating();

        // Units stay visible always; while operating they are applied live (they
        // are just conversion factors). Guard against non-positive values, which
        // the Drive rejects.
        ImGui::SeparatorText("Units");
        bool changed = false;
        changed |= ImGui::InputFloat("Encoder ticks / rev", &ticks_per_rev_);
        changed |= ImGui::InputFloat("Gear ratio",          &gear_ratio_);
        changed |= ImGui::InputFloat("Rated torque (Nm)",   &rated_torque_);
        bool units_ok = (ticks_per_rev_ > 0.0f) and (gear_ratio_ > 0.0f) and (rated_torque_ > 0.0f);
        if (not units_ok)
        {
            ImGui::TextColored(COLOR_RED, "units must be > 0");
        }
        else if (changed and operating_this)
        {
            CoE::CiA::DS402::UnitConfig u;
            u.encoder_ticks_per_rev = ticks_per_rev_;
            u.gear_ratio            = gear_ratio_;
            u.rated_torque_Nm       = rated_torque_;
            if (auto m = device.motor()) { m->setUnits(u); }  // applied live by the RT thread
        }

        if (operating_this)
        {
            renderControl(session, device);
        }
        else
        {
            synced_ = false;
            renderSetup(session, device, units_ok);
        }
    }

    void MotorPanel::renderSetup(BusSession& session, Device& device, bool units_ok)
    {
        if (session.isOperatingAny())
        {
            ImGui::TextDisabled("Bus is operating. Use \"Back to PRE-OP (all)\" above to change the "
                                "mapped set, then re-configure and Apply mapping.");
        }

        ImGui::SeparatorText("Bring-up");
        ImGui::Combo("Initial mode", &mode_index_, MODE_LABELS, IM_ARRAYSIZE(MODE_LABELS));
        ImGui::SetNextItemWidth(px(110.0f));
        ImGui::InputText("RxPDO map", rx_pdo_buf_, sizeof(rx_pdo_buf_));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(px(110.0f));
        ImGui::InputText("TxPDO map", tx_pdo_buf_, sizeof(tx_pdo_buf_));

        // Validate the PDO assignment indices up front (RxPDO 0x1600-0x17FF,
        // TxPDO 0x1A00-0x1BFF per CiA-301) instead of letting a stray 0x0000
        // fail deep inside bring-up as a generic error.
        long rx_pdo = std::strtol(rx_pdo_buf_, nullptr, 0);
        long tx_pdo = std::strtol(tx_pdo_buf_, nullptr, 0);
        bool rx_ok  = (rx_pdo >= 0x1600) and (rx_pdo <= 0x17FF);
        bool tx_ok  = (tx_pdo >= 0x1A00) and (tx_pdo <= 0x1BFF);
        bool pdo_ok = rx_ok and tx_ok;
        if (not pdo_ok)
        {
            ImGui::TextColored(COLOR_RED, "PDO map index out of range (RxPDO 0x1600-0x17FF, TxPDO 0x1A00-0x1BFF)");
        }

        // Record this drive's config into the operated set. The mapping is applied
        // bus-wide later via "Apply mapping" (the strip above); this button only
        // marks the drive to participate. Disabled while the bus is operating.
        ImGui::BeginDisabled(not units_ok or not pdo_ok or session.isOperatingAny());
        if (ImGui::Button("Include in mapping"))
        {
            OperateConfig cfg;
            cfg.units.encoder_ticks_per_rev = ticks_per_rev_;
            cfg.units.gear_ratio            = gear_ratio_;
            cfg.units.rated_torque_Nm       = rated_torque_;
            cfg.mode        = MODE_VALUES[mode_index_];
            cfg.rx_pdo_map  = static_cast<uint16_t>(rx_pdo);
            cfg.tx_pdo_map  = static_cast<uint16_t>(tx_pdo);
            device.configureSlave(cfg);
        }
        ImGui::EndDisabled();
        if (device.isConfigured())
        {
            ImGui::SameLine();
            ImGui::TextColored(COLOR_GREEN, "included \xe2\x80\x94 Apply mapping to operate");
            ImGui::SameLine();
            ImGui::BeginDisabled(session.isOperatingAny());
            if (ImGui::Button("Remove")) { device.unconfigureSlave(); }
            ImGui::EndDisabled();
        }

        // The motor error comes from the published snapshot, like the feedback.
        std::string error;
        auto snap = session.snapshot();
        if (snap and (device.index >= 0) and (device.index < static_cast<int>(snap->slaves.size())))
        {
            error = snap->slaves[device.index].motor_error;
        }
        if (not error.empty())
        {
            ImGui::Spacing();
            ImGui::TextColored(COLOR_RED, "%s", error.c_str());
        }
    }

    void MotorPanel::renderControl(BusSession& session, Device& device)
    {
        auto motor = device.motor();   // commands only (enable/setpoint/...)
        if (not motor)
        {
            return;  // not operating this device (shouldn't happen: caller gated on it)
        }
        // Feedback comes from the published snapshot: the UI never reaches into
        // the RT thread's state.
        DriveFeedback fb;
        auto snap = session.snapshot();
        if (snap and (device.index >= 0) and (device.index < static_cast<int>(snap->slaves.size())))
        {
            fb = snap->slaves[device.index].fb;
        }

        // --- state badge ---
        uint16_t sw = fb.status_word;
        if (fb.faulted)
        {
            ImGui::TextColored(COLOR_RED, "\xe2\x97\x8f FAULT");
        }
        else if (sw & status::masks::OPERATION_ENABLE)
        {
            ImGui::TextColored(COLOR_GREEN, "\xe2\x97\x8f OPERATION ENABLED");
        }
        else if (sw & status::masks::SWITCHED_ON)
        {
            ImGui::TextColored(COLOR_YELLOW, "\xe2\x97\x8f switched on");
        }
        else if (sw & status::masks::READY_TO_SWITCH_ON)
        {
            ImGui::TextColored(COLOR_YELLOW, "\xe2\x97\x8f ready to switch on");
        }
        else
        {
            ImGui::TextDisabled("\xe2\x97\x8f switch-on disabled");
        }
        ImGui::SameLine();
        ImGui::TextDisabled("status 0x%04X  ctrl 0x%04X  err 0x%04X", sw, fb.control_word, fb.error_code);

        // EtherCAT state is controlled by the common strip above the tabs.
        // The drive can only be enabled once the bus reaches OP. Reserve the line
        // height either way so the controls below don't jump when it appears.
        if (not fb.operational)
        {
            ImGui::TextColored(COLOR_YELLOW, "Not OPERATIONAL \xe2\x80\x94 set the device to OP (strip above) to enable.");
        }
        else
        {
            ImGui::NewLine();
        }

        // --- drive (DS402) ---
        // One energize toggle (the DS402 OPERATION-ENABLED bit) and an emergency
        // STOP (kills power AND any commanded motion). Clear fault appears only
        // while faulted. To stop driving entirely, set the device to PRE-OP on the
        // EtherCAT strip above (the shared ESM is the single state control).
        ImGui::SeparatorText("Drive");
        if (fb.enabled)
        {
            if (ImGui::Button("Disable")) { motor->enable(false); }
        }
        else
        {
            ImGui::BeginDisabled(not fb.operational);
            if (ImGui::Button("Enable")) { motor->enable(true); }
            ImGui::EndDisabled();
        }
        if (fb.faulted)
        {
            ImGui::SameLine();
            if (ImGui::Button("Clear fault"))
            {
                motor->enable(true);  // DS402 acknowledges the fault during the enable sequence
            }
            if (ImGui::IsItemHovered())
            {
                ImGui::SetTooltip("Acknowledges the fault and energizes the drive\n"
                                  "(DS402 clears faults during the enable sequence).");
            }
        }
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.6f, 0.1f, 0.12f, 1.0f));
        if (ImGui::Button("\xe2\x96\xa0 STOP"))
        {
            // Emergency: de-energize and neutralize the setpoint as ONE coherent
            // command so the RT loop never sees a half-applied stop.
            source_index_  = 0;  // also flip the UI selector, else it reverts next frame
            manual_target_ = safeManualTarget(fb);
            motor->edit([&](MotorControl::Command& c)
            {
                c.jog_rate      = 0.0;
                c.source        = static_cast<int>(SetpointSource::Manual);
                c.manual_target = manual_target_;
                c.enable        = false;
            });
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("Emergency stop: de-energize and zero the setpoint.");
        }
        // To stop driving this device entirely, set it to PRE-OP on the EtherCAT
        // strip above -- that releases it from the cyclic loop (shared ESM is the
        // single state control; no separate "release" action).

        // --- mode ---
        ImGui::SeparatorText("Mode");
        if (ImGui::Combo("##mode", &mode_index_, MODE_LABELS, IM_ARRAYSIZE(MODE_LABELS)))
        {
            // The Manual target's units changed with the mode; reset it to a safe
            // value and apply mode+target+jog as ONE coherent command.
            manual_target_ = safeManualTarget(fb);
            motor->edit([&](MotorControl::Command& c)
            {
                c.mode          = static_cast<int>(MODE_VALUES[mode_index_]);
                c.manual_target = manual_target_;
                c.jog_rate      = 0.0;
            });
        }
        ImGui::SameLine();
        ImGui::TextDisabled("display: %d", fb.mode_display);

        // --- setpoint ---
        ImGui::SeparatorText("Setpoint");
        char const* unit = setpointUnit(mode_index_);

        auto sendSource = [&](SetpointSource s)
        {
            if (sent_source_ != static_cast<int>(s))
            {
                motor->setSetpointSource(s);
                sent_source_ = static_cast<int>(s);
            }
        };

        // Fixed-height area sized for the tallest source (Generator) so the
        // Feedback table below never jumps when the source or OP state changes.
        ImGui::BeginChild("##setpoint_area", ImVec2(0.0f, ImGui::GetFrameHeightWithSpacing() * 7.0f), false);
        if (not fb.operational)
        {
            ImGui::TextDisabled("waiting for operational state...");
            // Left OP (e.g. dropped to SAFE-OP): stop driving from the generator/jog
            // (once) so the waveform doesn't keep advancing and the motor won't jump
            // when it returns to OP. Re-seed the hold point on the way back in.
            if (not hold_sent_)
            {
                gen_running_ = false;
                jog_current_ = 0.0f;
                synced_      = false;
                motor->edit([](MotorControl::Command& c)
                {
                    c.source   = static_cast<int>(SetpointSource::Manual);
                    c.jog_rate = 0.0;
                });
                sent_source_ = static_cast<int>(SetpointSource::Manual);
                sent_jog_    = 0.0f;
                hold_sent_   = true;
            }
        }
        else
        {
            hold_sent_ = false;
            // Seed the panel's setpoint and the live command to the actual
            // position once, the instant the drive is operational, so enabling
            // (or starting the generator) holds position instead of slamming to a
            // stale target captured earlier at SAFE-OP.
            if (not synced_)
            {
                manual_target_ = safeManualTarget(fb);
                motor->setManualTarget(manual_target_);
                synced_ = true;
            }

            ImGui::RadioButton("Manual", &source_index_, 0);
            ImGui::SameLine();
            ImGui::RadioButton("Jog", &source_index_, 1);
            ImGui::SameLine();
            ImGui::RadioButton("Generator", &source_index_, 2);

            if (source_index_ == 0)
            {
                sendSource(SetpointSource::Manual);
                ImGui::InputFloat("target", &manual_target_);
                if (ImGui::IsItemDeactivatedAfterEdit())  // commit on edit-complete, like the generator center
                {
                    motor->setManualTarget(manual_target_);
                }
                ImGui::SameLine();
                ImGui::TextDisabled("%s", unit);
                if (ImGui::Button("Send"))
                {
                    motor->setManualTarget(manual_target_);
                }
                ImGui::SameLine();
                if (ImGui::Button("= actual"))
                {
                    manual_target_ = static_cast<float>(fb.actual_pos);
                    motor->setManualTarget(manual_target_);
                }
                // Slew rate toward the target so a new setpoint ramps instead of
                // stepping (0 = step immediately).
                ImGui::SetNextItemWidth(px(120.0f));
                ImGui::InputFloat("ramp (/s)", &ramp_rate_);
                if (ramp_rate_ < 0.0f) { ramp_rate_ = 0.0f; }
                if (ramp_rate_ != sent_ramp_)
                {
                    motor->setManualRampRate(ramp_rate_);
                    sent_ramp_ = ramp_rate_;
                }
            }
            else if (source_index_ == 1)
            {
                sendSource(SetpointSource::Jog);
                ImGui::InputFloat("jog rate", &jog_rate_);
                ImGui::SameLine();
                ImGui::TextDisabled("%s", unit);
                ImGui::SetNextItemWidth(px(120.0f));
                ImGui::InputFloat("jog ramp (/s\xc2\xb2)", &jog_accel_);
                if (jog_accel_ < 0.0f) { jog_accel_ = 0.0f; }

                ImGui::Button("\xe2\x97\x80 jog \xe2\x88\x92");
                bool minus = ImGui::IsItemActive();
                ImGui::SameLine();
                ImGui::Button("jog + \xe2\x96\xb6");
                bool plus = ImGui::IsItemActive();

                // Trapezoid: accelerate the commanded rate toward +/-jog_rate while
                // held and decelerate back to 0 on release (0 ramp = step to rate).
                float target = 0.0f;
                if (plus)       { target = jog_rate_; }
                else if (minus) { target = -jog_rate_; }
                if (jog_accel_ <= 0.0f)
                {
                    jog_current_ = target;
                }
                else
                {
                    float step = jog_accel_ * ImGui::GetIO().DeltaTime;
                    if (jog_current_ < target)
                    {
                        jog_current_ += step;
                        if (jog_current_ > target) { jog_current_ = target; }
                    }
                    else if (jog_current_ > target)
                    {
                        jog_current_ -= step;
                        if (jog_current_ < target) { jog_current_ = target; }
                    }
                }
                if (jog_current_ != sent_jog_)   // streams while ramping, silent once settled
                {
                    motor->setJog(jog_current_);
                    sent_jog_ = jog_current_;
                }
            }
            else
            {
                ImGui::Combo("waveform", &wave_index_, WAVE_LABELS, IM_ARRAYSIZE(WAVE_LABELS));
                ImGui::InputFloat("amplitude", &gen_amp_);
                ImGui::InputFloat("frequency (Hz)", &gen_freq_);
                if (mode_index_ == 0)
                {
                    ImGui::InputFloat("center", &manual_target_);
                    // Commit on edit-complete, while running only: paused, the hold
                    // target must stand (the RT loop re-centers on play anyway).
                    if (ImGui::IsItemDeactivatedAfterEdit() and gen_running_)
                    {
                        motor->setManualTarget(manual_target_);
                    }
                }
                else
                {
                    ImGui::InputFloat("offset", &gen_offset_);
                }

                // Play/Pause: the generator only advances while playing AND the drive
                // is enabled -- pausing (or a disable) holds position so the waveform
                // doesn't run continuously or jump on resume.
                char const* play_label = "\xe2\x96\xb6 Play";
                if (gen_running_) { play_label = "Pause"; }
                ImGui::BeginDisabled(not fb.enabled);   // the generator only runs once enabled
                if (ImGui::Button(play_label)) { gen_running_ = not gen_running_; }
                ImGui::EndDisabled();
                ImGui::SameLine();
                if (not fb.enabled)   { ImGui::TextDisabled("enable the drive first"); }
                else if (gen_running_) { ImGui::TextColored(COLOR_GREEN, "running"); }
                else                  { ImGui::TextDisabled("paused"); }

                if (gen_running_ and fb.enabled)
                {
                    sendSource(SetpointSource::Generator);
                    bool gen_changed = (wave_index_ != sent_wave_) or (gen_amp_ != sent_amp_)
                                    or (gen_freq_ != sent_freq_) or (gen_offset_ != sent_goffset_);
                    if (gen_changed)
                    {
                        motor->setGenerator(static_cast<Waveform>(wave_index_), gen_amp_, gen_freq_, gen_offset_);
                        sent_wave_    = wave_index_;
                        sent_amp_     = gen_amp_;
                        sent_freq_    = gen_freq_;
                        sent_goffset_ = gen_offset_;
                    }
                }
                else
                {
                    if (not fb.enabled) { gen_running_ = false; }
                    // Pause/disable: hold the position captured NOW, as one command.
                    bool leaving_generator = (sent_source_ == static_cast<int>(SetpointSource::Generator));
                    sendSource(SetpointSource::Manual);
                    if (leaving_generator)
                    {
                        motor->setManualTarget(safeManualTarget(fb));
                    }
                }
            }
        }
        ImGui::EndChild();

        // --- readouts ---
        ImGui::SeparatorText("Feedback");
        if (ImGui::BeginTable("##fb", 3,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("quantity", ImGuiTableColumnFlags_WidthFixed, px(90.0f));
            ImGui::TableSetupColumn("raw", ImGuiTableColumnFlags_WidthFixed, px(130.0f));
            ImGui::TableSetupColumn("SI");
            ImGui::TableHeadersRow();

            auto row = [](char const* k, std::string const& raw, std::string const& si)
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextDisabled("%s", k);
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(raw.c_str());
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(si.c_str());
            };

            char raw[48];
            char si[48];
            std::snprintf(raw, sizeof(raw), "%d ticks", fb.actual_pos_raw);
            std::snprintf(si,  sizeof(si),  "%.4f rad", fb.actual_pos);
            row("position", raw, si);
            std::snprintf(raw, sizeof(raw), "%d", fb.actual_vel_raw);
            std::snprintf(si,  sizeof(si),  "%.4f rad/s", fb.actual_vel);
            row("velocity", raw, si);
            std::snprintf(raw, sizeof(raw), "%d", fb.actual_torque_raw);
            std::snprintf(si,  sizeof(si),  "%.4f Nm", fb.actual_torque);
            row("torque", raw, si);
            std::snprintf(si,  sizeof(si),  "%.4f %s", fb.target, unit);
            row("target", "\xe2\x80\x94", si);
            ImGui::EndTable();
        }
    }
}
