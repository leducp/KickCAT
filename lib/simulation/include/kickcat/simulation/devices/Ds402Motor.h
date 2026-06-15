#ifndef KICKCAT_SIMULATION_DS402_MOTOR_H
#define KICKCAT_SIMULATION_DS402_MOTOR_H

#include <algorithm>
#include <cstdint>

#include "kickcat/CoE/CiA/DS402/protocol.h"
#include "kickcat/simulation/DeviceApp.h"
#include "kickcat/slave/Slave.h"

namespace kickcat::sim
{
    namespace ds402 = CoE::CiA::DS402;

    // Slave-side CiA-402 motor emulation for the network simulator. It runs the
    // controlword -> statusword state machine a master's DS402 StateMachine expects
    // (SWITCH-ON-DISABLED -> READY -> SWITCHED-ON -> OPERATION-ENABLED) and, once
    // enabled, runs a small motor plant so the master sees realistic dynamics
    // (following error, finite bandwidth, torque-driven motion) instead of perfect
    // instantaneous tracking. Binds to the PDO-mapped object entries after SAFE-OP.
    class Ds402Motor : public DeviceApp
    {
    public:
        // Plant tuning (per cycle). Defaults give a stable, visibly-lagging response;
        // override per slave from the JSON config.
        struct Params
        {
            double cycle_s       = 0.001;   // integration step per cyclic frame
            double pos_bandwidth = 80.0;    // rad/s, CSP closed-loop natural frequency
            double damping       = 1.0;     // CSP damping ratio (1 = critically damped)
            double vel_tau       = 0.010;   // s, CSV velocity-loop time constant
            double inertia       = 5.0e-5;  // CST: accel = (torque - friction*vel) / inertia
            double friction      = 1.0e-3;  // viscous friction
            double torque_scale  = 1.0;     // raw target_torque -> plant torque
            double max_velocity  = 0.0;     // ticks/s clamp, 0 = unlimited
        };

        explicit Ds402Motor(slave::Slave& slave) : slave_(&slave) {}
        Ds402Motor(slave::Slave& slave, Params params) : slave_(&slave), params_(params)
        {
            // Divisors in the integrator: a degenerate config (0) would blow pos_/
            // vel_ up to NaN. Fall back to the defaults rather than divide by zero.
            Params const def;
            if (params_.cycle_s <= 0.0) { params_.cycle_s = def.cycle_s; }
            if (params_.vel_tau <= 0.0) { params_.vel_tau = def.vel_tau; }
            if (params_.inertia <= 0.0) { params_.inertia = def.inertia; }
        }

        void step() override
        {
            State s = slave_->state();
            if (s != State::SAFE_OP and s != State::OPERATIONAL)
            {
                bound_ = false;  // mapping is torn down below SAFE-OP; re-bind on re-entry
                return;
            }

            if (not bound_)
            {
                bind();
            }
            if (not bound_)
            {
                return;
            }

            stepStateMachine(*control_word_);
            *status_word_ = status_word_value_;
            if (mode_display_ != nullptr and mode_ != nullptr)
            {
                *mode_display_ = *mode_;
            }

            integrateMotion(status_word_value_ == OPERATION_ENABLED);
        }

    private:
        // CiA-402 state statuswords, composed from the canonical bits so the
        // values stay in sync with the master's DS402 StateMachine.
        static constexpr uint16_t SWITCH_ON_DISABLED = ds402::status::masks::SWITCH_ON_DISABLED;
        static constexpr uint16_t READY              = ds402::status::masks::READY_TO_SWITCH_ON
                                                     | ds402::status::masks::VOLTAGE_ENABLED
                                                     | ds402::status::masks::QUICK_STOP;
        static constexpr uint16_t SWITCHED_ON        = READY | ds402::status::masks::SWITCHED_ON;
        static constexpr uint16_t OPERATION_ENABLED  = SWITCHED_ON | ds402::status::masks::OPERATION_ENABLE;

        void bind()
        {
            slave_->bind(0x6040, control_word_);
            slave_->bind(0x6041, status_word_);
            slave_->bind(0x6060, mode_);
            slave_->bind(0x6061, mode_display_);
            slave_->bind(0x607A, target_position_);
            slave_->bind(0x6064, actual_position_);
            slave_->bind(0x60FF, target_velocity_);
            slave_->bind(0x606C, actual_velocity_);
            slave_->bind(0x6071, target_torque_);
            slave_->bind(0x6077, actual_torque_);

            // The RxPDO controlword and the TxPDO statusword are the minimum needed
            // to follow a master through the enable sequence.
            bound_ = (control_word_ != nullptr and status_word_ != nullptr);
            if (actual_position_ != nullptr)
            {
                pos_ = static_cast<double>(*actual_position_);  // start the plant at the current feedback
            }
            vel_ = 0.0;
        }

        void stepStateMachine(uint16_t control_word)
        {
            // Enable-voltage bit clear (covers DISABLE_VOLTAGE) -> de-energize.
            if ((control_word & 0x0002) == 0)
            {
                status_word_value_ = SWITCH_ON_DISABLED;
                return;
            }

            // Honour the CiA-402 transition prerequisites: a command only advances
            // from a valid prior state, so a stray ENABLE_OPERATION out of
            // SWITCH-ON-DISABLED cannot shortcut straight to OPERATION-ENABLED.
            uint16_t command = control_word & 0x000F;
            if (command == 0x0006)        // SHUTDOWN -> READY
            {
                if (status_word_value_ == SWITCH_ON_DISABLED or status_word_value_ == SWITCHED_ON
                    or status_word_value_ == OPERATION_ENABLED or status_word_value_ == READY)
                {
                    status_word_value_ = READY;
                }
            }
            else if (command == 0x0007)   // SWITCH ON -> SWITCHED ON (from READY / OP-ENABLED)
            {
                if (status_word_value_ == READY or status_word_value_ == OPERATION_ENABLED)
                {
                    status_word_value_ = SWITCHED_ON;
                }
            }
            else if (command == 0x000F)   // ENABLE OPERATION -> OPERATION ENABLED (auto from READY)
            {
                if (status_word_value_ == READY or status_word_value_ == SWITCHED_ON)
                {
                    status_word_value_ = OPERATION_ENABLED;
                }
            }
            // any other command: hold the current state
        }

        // Advance the motor plant one cycle. The drive's inner loop produces an
        // acceleration from the active mode's setpoint; a single inertia state then
        // integrates it, so the master observes lag/following error rather than an
        // instantaneous jump.
        void integrateMotion(bool enabled)
        {
            int8_t mode = static_cast<int8_t>(ds402::control::POSITION_CYCLIC);
            if (mode_ != nullptr)
            {
                mode = *mode_;
            }

            double const dt = params_.cycle_s;
            double accel = 0.0;

            if (not enabled)
            {
                vel_ = 0.0;  // de-energized: stop and hold position
            }
            else if (mode == ds402::control::VELOCITY_CYCLIC)
            {
                double target_v = 0.0;
                if (target_velocity_ != nullptr) { target_v = static_cast<double>(*target_velocity_); }
                accel = (target_v - vel_) / params_.vel_tau;        // first-order velocity loop
            }
            else if (mode == ds402::control::TORQUE_CYCLIC)
            {
                double torque = 0.0;
                if (target_torque_ != nullptr) { torque = static_cast<double>(*target_torque_) * params_.torque_scale; }
                accel = (torque - params_.friction * vel_) / params_.inertia;  // inertia + friction
            }
            else  // CSP: critically-damped second-order tracking of target_position
            {
                double target_p = pos_;
                if (target_position_ != nullptr) { target_p = static_cast<double>(*target_position_); }
                double const wn = params_.pos_bandwidth;
                accel = wn * wn * (target_p - pos_) - 2.0 * params_.damping * wn * vel_;
            }

            if (enabled)
            {
                vel_ += accel * dt;
                if (params_.max_velocity > 0.0)
                {
                    vel_ = std::clamp(vel_, -params_.max_velocity, params_.max_velocity);
                }
                pos_ += vel_ * dt;
            }

            if (actual_position_ != nullptr) { *actual_position_ = static_cast<int32_t>(pos_); }
            if (actual_velocity_ != nullptr) { *actual_velocity_ = static_cast<int32_t>(vel_); }
            if (actual_torque_ != nullptr)
            {
                // Torque the motor experiences; for CST this equals the commanded torque.
                double torque = params_.inertia * accel + params_.friction * vel_;
                torque = std::clamp(torque, -32768.0, 32767.0);
                *actual_torque_ = static_cast<int16_t>(torque);
            }
        }

        slave::Slave* slave_;
        Params        params_{};
        bool          bound_{false};
        uint16_t      status_word_value_{SWITCH_ON_DISABLED};
        double        pos_{0.0};  // plant state: position (ticks)
        double        vel_{0.0};  // plant state: velocity (ticks/s)

        uint16_t* control_word_{nullptr};
        uint16_t* status_word_{nullptr};
        int8_t*   mode_{nullptr};
        int8_t*   mode_display_{nullptr};
        int32_t*  target_position_{nullptr};
        int32_t*  actual_position_{nullptr};
        int32_t*  target_velocity_{nullptr};
        int32_t*  actual_velocity_{nullptr};
        int16_t*  target_torque_{nullptr};
        int16_t*  actual_torque_{nullptr};
    };
}

#endif
