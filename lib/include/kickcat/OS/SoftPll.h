#ifndef KICKCAT_OS_SOFT_PLL_H
#define KICKCAT_OS_SOFT_PLL_H

#include <cstdint>

#include "kickcat/OS/Time.h"

namespace kickcat
{
    /// \brief Software PLL that phase-locks a cyclic loop to a free-running reference clock.
    /// \details Disciplines the phase of a local cadence (a Timer's deadline grid). Owned by Timer;
    ///          feed it the reference slave's system time each cycle via Timer::sync_to.
    ///
    ///          Each cycle it is fed the reference clock's time and whether the previous cycle woke
    ///          late. It computes the phase error of the wake instant inside the cycle grid, runs an
    ///          EMA-filtered clamped PI controller (P pulls phase, I learns the ppm frequency
    ///          offset), rejects overrun/outlier samples, and returns a slew-limited phase correction.
    class SoftPll
    {
    public:
        struct Config
        {
            double      ema_alpha        = 0.1;      // EMA weight on the newest phase error
            double      kp               = 0.05;     // proportional gain (phase pull)
            double      ki               = 0.0005;   // integral gain (learns frequency offset)
            nanoseconds slew_limit       = 20us;     // max |correction| applied per cycle
            nanoseconds integrator_clamp = 200us;    // anti-windup bound on the integral term
            nanoseconds outlier_reject   = 150us;    // |phase error| above this -> coast, do not learn
            nanoseconds lock_threshold   = 8us;      // |EMA| below this counts toward lock
            uint32_t    lock_samples     = 500;      // consecutive in-threshold samples to declare lock
            // Unlock hysteresis: once locked, lock is only dropped after |EMA| stays above
            // unlock_threshold (> lock_threshold) for unlock_samples in a row. Without this the
            // lock flaps between locked/acquiring whenever the filtered error rides the threshold.
            nanoseconds unlock_threshold = 20us;     // |EMA| above this counts toward unlock
            uint32_t    unlock_samples   = 200;      // consecutive over-threshold samples to drop lock
            // consecutive post-lock outlier rejects before dropping the lock and re-acquiring
            uint32_t    reject_escape_samples = 200;
        };

        /// \param cycle_period  the loop period; the grid the phase is measured against
        /// \param config        gains and limits; defaults are the hardware-validated starting point
        // Two overloads instead of a Config{} default argument: a nested struct's member
        // initializers are not usable in the enclosing class's complete-class context.
        explicit SoftPll(nanoseconds cycle_period);
        SoftPll(nanoseconds cycle_period, Config const& config);

        /// \brief Consume one cycle's reference-clock sample and advance the controller.
        /// \param reference_system_time  free-running reference clock, raw ns
        /// \param cycle_overran          previous cycle woke late; its sample is rejected as an outlier
        /// \return the phase correction to apply to the loop deadline. Its sign is already oriented to
        ///         shrink the phase error; a coasted cycle re-applies the last correction so the
        ///         frequency bias is held.
        nanoseconds update(uint64_t reference_system_time, bool cycle_overran);

        /// \brief Coast one cycle with no fresh sample: re-apply the last correction, do not learn.
        /// \return the held phase correction (same sign as update()).
        nanoseconds coast() const { return nanoseconds{-correction_}; }

        /// \return true once |EMA| has stayed below lock_threshold for lock_samples cycles in a row
        bool locked() const { return locked_; }

        /// \brief Drop the learned state (target, integrator, filter, lock). The next valid sample
        ///        relearns the target phase.
        void reset();

        /// \return last raw (wrapped) phase error; valid once at least one sample was accepted
        nanoseconds phase_error() const { return nanoseconds{last_error_}; }

        /// \return the EMA-filtered phase error in ns (double, sub-ns resolution kept for the loop)
        double filtered_error() const { return ema_; }

        /// \return the phase correction applied to the loop deadline last cycle (ns).
        nanoseconds correction() const { return nanoseconds{-correction_}; }

        /// \return the learned frequency offset (ppm) of the local cadence vs the reference;
        ///         positive means the reference runs ahead. Meaningful once locked.
        double frequency_offset_ppm() const
        {
            if (cycle_ <= 0)
            {
                return 0.0;
            }
            return ki_ * integ_ / static_cast<double>(cycle_) * 1e6;
        }

        /// \return number of samples accepted since construction/reset
        uint64_t samples() const { return samples_; }

    private:
        int64_t cycle_;              // loop period in ns
        double  ema_alpha_;
        double  kp_;
        double  ki_;
        int64_t slew_;               // per-cycle correction clamp, ns
        int64_t integ_clamp_;        // anti-windup clamp, ns
        int64_t reject_;             // outlier reject threshold, ns
        int64_t lock_threshold_;     // ns
        uint32_t lock_samples_;
        int64_t  unlock_threshold_;  // ns; unlock hysteresis amplitude
        uint32_t unlock_samples_;    // consecutive over-threshold samples to drop lock
        uint32_t reject_escape_;     // consecutive post-lock outlier rejects before re-acquiring

        int64_t target_{-1};         // learned target phase, ns; <0 means "not learned yet"
        int64_t last_error_{0};
        double  ema_{0.0};
        double  integ_{0.0};
        int64_t correction_{0};      // last PI output (sign = phase direction), ns
        uint64_t samples_{0};
        uint32_t lock_count_{0};
        uint32_t unlock_count_{0};
        uint32_t reject_streak_{0};   // consecutive post-lock outlier rejects, for the escape
        bool    locked_{false};
        bool    ever_locked_{false};  // gates the outlier reject: off during cold acquisition

    };
}

#endif
