#include "kickcat/OS/SoftPll.h"
#include "kickcat/OS/math.h"

namespace kickcat
{
    SoftPll::SoftPll(nanoseconds cycle_period)
        : SoftPll(cycle_period, Config{})
    {
    }

    SoftPll::SoftPll(nanoseconds cycle_period, Config const& config)
        : cycle_(cycle_period.count())
        , ema_alpha_(config.ema_alpha)
        , kp_(config.kp)
        , ki_(config.ki)
        , slew_(config.slew_limit.count())
        , integ_clamp_(config.integrator_clamp.count())
        , reject_(config.outlier_reject.count())
        , lock_threshold_(config.lock_threshold.count())
        , lock_samples_(config.lock_samples)
        , unlock_threshold_(config.unlock_threshold.count())
        , unlock_samples_(config.unlock_samples)
        , reject_escape_(config.reject_escape_samples)
    {
    }

    void SoftPll::reset()
    {
        target_     = -1;
        last_error_ = 0;
        ema_        = 0.0;
        integ_      = 0.0;
        correction_  = 0;
        samples_     = 0;
        lock_count_  = 0;
        unlock_count_ = 0;
        reject_streak_ = 0;
        locked_      = false;
        ever_locked_ = false;
    }

    nanoseconds SoftPll::update(uint64_t reference_system_time, bool cycle_overran)
    {
        // Guard against a zero/garbage period: a valid cycle grid is required to measure phase.
        if (cycle_ <= 0)
        {
            return nanoseconds{0};
        }

        int64_t const phase = static_cast<int64_t>(reference_system_time % static_cast<uint64_t>(cycle_));

        // Learn the target phase from the first valid sample; the constant master/reference offset
        // and propagation delays are absorbed here so only the residual jitter is disciplined.
        if (target_ < 0)
        {
            target_ = phase;
        }

        // Wrap the error into (-cycle/2, +cycle/2] so a phase near the grid boundary is corrected
        // the short way round rather than chasing a full cycle.
        int64_t error = phase - target_;
        if (error > cycle_ / 2)
        {
            error -= cycle_;
        }
        if (error <= -cycle_ / 2)
        {
            error += cycle_;
        }
        last_error_ = error;
        ++samples_;

        // Coast (don't learn) on an overrun or a post-lock outlier. The outlier gate is armed only
        // after first lock: pre-lock a large error is the startup offset to slew in, not noise.
        bool reject = cycle_overran;
        if (ever_locked_ and abs_value(error) > reject_)
        {
            reject = true;

            // A run of post-lock outliers means the reference moved: drop the stale lock and
            // re-acquire rather than coast forever. A lone glitch just coasts (streak resets below).
            ++reject_streak_;
            if (reject_streak_ >= reject_escape_)
            {
                target_       = -1;
                ema_          = 0.0;
                integ_        = 0.0;
                correction_   = 0;
                lock_count_   = 0;
                unlock_count_ = 0;
                reject_streak_ = 0;
                locked_       = false;
                ever_locked_  = false;
            }
        }
        else
        {
            reject_streak_ = 0;
        }

        if (not reject)
        {
            ema_   = ema_alpha_ * static_cast<double>(error) + (1.0 - ema_alpha_) * ema_;
            integ_ = clamp(integ_ + static_cast<double>(error),
                           -static_cast<double>(integ_clamp_), static_cast<double>(integ_clamp_));
            double const u = kp_ * ema_ + ki_ * integ_;
            correction_ = clamp(round_to_int(u), -slew_, slew_);

            // Lock detector with hysteresis. An application gates its readiness on locked(), so it
            // must not chatter: lock is declared once the filtered error has been predominantly in
            // band, and dropped only after it stays above a HIGHER unlock threshold for a sustained
            // window. The acquisition counter LEAKS rather than hard-resetting. An occasional
            // out-of-band sample only costs one step, so jitter (a soft-RT master) merely slows lock
            // instead of blocking it forever, while a genuinely unconverged loop still never gets
            // there. Only accepted samples move the counters, and a rejected sample carries no fresh
            // phase evidence, so it neither advances nor breaks the lock.
            int64_t const abs_ema = abs_value(static_cast<int64_t>(ema_));
            if (not locked_)
            {
                if (abs_ema < lock_threshold_)
                {
                    ++lock_count_;
                    if (lock_count_ >= lock_samples_)
                    {
                        locked_       = true;
                        ever_locked_  = true;
                        unlock_count_ = 0;
                    }
                }
                else if (lock_count_ > 0)
                {
                    --lock_count_;
                }
            }
            else
            {
                if (abs_ema > unlock_threshold_)
                {
                    ++unlock_count_;
                }
                else
                {
                    unlock_count_ = 0;
                }

                if (unlock_count_ >= unlock_samples_)
                {
                    locked_     = false;
                    lock_count_ = 0;
                }
            }
        }

        // error>0: reference phase is ahead of target -> the wake is late in the grid -> pull the
        // next deadline earlier (negative delta). Sign validated on hardware: an injected positive
        // offset must decay, not grow. A coasted cycle re-applies the last correction to hold bias.
        return nanoseconds{-correction_};
    }
}
