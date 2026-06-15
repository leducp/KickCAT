#ifndef KICKCAT_TOOLS_KICKUI_MOTOR_PANEL_H
#define KICKCAT_TOOLS_KICKUI_MOTOR_PANEL_H

#include "Panel.h"

namespace kickcat::kickui
{
    struct DriveFeedback;

    // DS402 drive control: bring-up, state machine, jog/step, motion generator.
    class MotorPanel : public Panel
    {
    public:
        char const* title() const override { return "Control"; }
        bool appliesTo(Device const& device) const override;
        void render(BusSession& session, Device& device) override;

    private:
        void renderSetup(BusSession& session, Device& device, bool units_ok);
        void renderControl(BusSession& session, Device& device);

        // Safe Manual setpoint for the current mode: hold actual position in CSP,
        // zero (stop) in velocity/torque modes (different units per mode).
        float safeManualTarget(DriveFeedback const& fb) const;

        // Bring-up parameters (UnitConfig + initial mode).
        float ticks_per_rev_ = 524288.0f;
        float gear_ratio_    = 1.0f;
        float rated_torque_  = 1.0f;
        int   mode_index_    = 0;   // 0=CSP, 1=CSV, 2=CST
        char  rx_pdo_buf_[12] = "0x1600";
        char  tx_pdo_buf_[12] = "0x1A00";

        // Setpoint controls.
        int   source_index_ = 0;    // 0=Manual, 1=Jog, 2=Generator
        float manual_target_ = 0.0f;
        float ramp_rate_     = 1.0f; // manual slew rate (unit/s); 0 = step
        float jog_rate_      = 1.0f;
        float jog_accel_     = 5.0f; // jog ramp (unit/s^2); 0 = step to rate
        float jog_current_   = 0.0f; // currently-commanded jog rate (ramped)
        int   wave_index_    = 0;    // 0=Sine, 1=Step, 2=Triangle
        float gen_amp_       = 0.5f;
        float gen_freq_      = 0.5f;
        float gen_offset_    = 0.0f;
        bool  gen_running_   = false; // generator play/pause (paused holds position)

        // True once the setpoint fields have been synced to the live position for
        // the current operation (cleared when the drive leaves OP).
        bool  synced_ = false;

        // Last values pushed to the bus actor; verbs are submitted on change only.
        int   sent_source_  = -1;
        float sent_ramp_    = -1.0f;   // ramp is clamped >= 0, so -1 = nothing sent yet
        float sent_jog_     = 0.0f;
        int   sent_wave_    = -1;
        float sent_amp_     = 0.0f;
        float sent_freq_    = 0.0f;
        float sent_goffset_ = 0.0f;
        bool  hold_sent_    = false;   // the leave-OP stop command went out
    };
}

#endif
