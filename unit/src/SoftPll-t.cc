// SoftPll: closed-loop convergence and sign of the phase correction.
//
// Plant model. The reference clock free-runs; the master wakes at t_k and samples it. Nudging the
// deadline by delta shifts the next wake, so with ref_ns = t_k + phi:
//     phase_{k+1} = (phase_k + delta_k) mod cycle
// The controller must drive an injected phase step back to zero. delta carries the opposite sign of
// the error, so this also verifies the correction sign (a positive offset must decay, not grow).
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <cstdlib>

#include "kickcat/OS/SoftPll.h"

using namespace kickcat;

namespace
{
    // Advance the plant one cycle: feed the current reference sample, apply the returned
    // correction to the wake time, and return the raw wrapped phase error the loop measured.
    int64_t stepPlant(SoftPll& disc, int64_t& wake_ns, int64_t injected_ns, int64_t cycle_ns)
    {
        uint64_t const ref = static_cast<uint64_t>(wake_ns + injected_ns);
        nanoseconds const delta = disc.update(ref, false);
        wake_ns += cycle_ns + delta.count();
        return disc.phase_error().count();
    }
}


TEST(SoftPllTest, learns_target_and_holds_zero_when_undisturbed)
{
    int64_t const cycle = 1000000; // 1 ms
    SoftPll disc{nanoseconds{cycle}};

    int64_t wake = 5000000;
    for (int i = 0; i < 10; ++i)
    {
        int64_t const err = stepPlant(disc, wake, 0, cycle);
        EXPECT_EQ(0, err); // no disturbance -> error stays at the learned target
    }
}


TEST(SoftPllTest, positive_offset_decays_and_locks)
{
    int64_t const cycle = 1000000; // 1 ms
    SoftPll disc{nanoseconds{cycle}};

    // Cycle 0 establishes the target phase (error 0).
    int64_t wake = 5000000;
    stepPlant(disc, wake, 0, cycle);

    // Inject a +50 us phase step, held for the rest of the run.
    int64_t const injected = 50000;

    // First disturbed cycle: error must be positive and the correction must pull the wake earlier
    // (negative delta) -- the sign check.
    uint64_t const ref = static_cast<uint64_t>(wake + injected);
    nanoseconds const first_delta = disc.update(ref, false);
    EXPECT_GT(disc.phase_error().count(), 0);      // +offset shows up as +error
    EXPECT_LT(first_delta.count(), 0);            // correction opposes it
    wake += cycle + first_delta.count();

    int64_t const initial_err = disc.phase_error().count();

    // Run the loop closed; the step must decay toward zero.
    int64_t last_err = initial_err;
    for (int i = 0; i < 4000; ++i)
    {
        last_err = stepPlant(disc, wake, injected, cycle);
    }

    EXPECT_LT(std::llabs(last_err), std::llabs(initial_err)); // it shrank
    EXPECT_LT(std::llabs(last_err), 1000);                    // to well under 1 us
    EXPECT_TRUE(disc.locked());                               // sustained small error asserts lock
}


TEST(SoftPllTest, negative_offset_decays_with_opposite_sign_correction)
{
    int64_t const cycle = 1000000;
    SoftPll disc{nanoseconds{cycle}};

    int64_t wake = 5000000;
    stepPlant(disc, wake, 0, cycle);

    int64_t const injected = -50000; // -50 us

    uint64_t const ref = static_cast<uint64_t>(wake + injected);
    nanoseconds const first_delta = disc.update(ref, false);
    EXPECT_LT(disc.phase_error().count(), 0);
    EXPECT_GT(first_delta.count(), 0); // correction opposes the negative error
    wake += cycle + first_delta.count();

    int64_t last_err = disc.phase_error().count();
    for (int i = 0; i < 4000; ++i)
    {
        last_err = stepPlant(disc, wake, injected, cycle);
    }
    EXPECT_LT(std::llabs(last_err), 1000);
    EXPECT_TRUE(disc.locked());
}


TEST(SoftPllTest, learns_frequency_offset_ppm)
{
    int64_t const cycle = 1000000; // 1 ms
    SoftPll disc{nanoseconds{cycle}};

    // Reference runs faster than the master cadence by a constant rate: model it as an offset
    // proportional to elapsed time. The loop must learn this bias, and frequency_offset_ppm() must
    // report it (this is the drift figure a soak test watches).
    double const ppm = 20.0;
    int64_t wake = 5000000;
    for (int i = 0; i < 60000; ++i)
    {
        int64_t const ref = wake + static_cast<int64_t>(wake * ppm * 1e-6);
        nanoseconds const delta = disc.update(static_cast<uint64_t>(ref), false);
        wake += cycle + delta.count();
    }

    EXPECT_TRUE(disc.locked());
    EXPECT_NEAR(disc.frequency_offset_ppm(), ppm, 3.0); // learned bias tracks the injected drift
    EXPECT_LT(std::fabs(disc.filtered_error()), 1000.0); // residual phase error sub-us
}


TEST(SoftPllTest, cold_start_beyond_reject_window_still_locks)
{
    int64_t const cycle = 1000000; // 1 ms
    SoftPll disc{nanoseconds{cycle}};

    int64_t wake = 5000000;
    stepPlant(disc, wake, 0, cycle); // cycle 0 learns the target at error 0

    // A cold-start phase offset far beyond the 150 us outlier-reject window. Before the staged
    // reject this was discarded every cycle and the loop never engaged; now it must slew in and
    // lock within a bounded, sub-second window (slew-limited pull-in + lock_samples).
    int64_t const injected = 400000; // 400 us > outlier_reject (150 us)
    int locked_at = -1;
    for (int i = 0; i < 1000; ++i)
    {
        stepPlant(disc, wake, injected, cycle);
        if (disc.locked())
        {
            locked_at = i;
            break;
        }
    }
    EXPECT_GE(locked_at, 0);   // it locked at all (old behavior: never)
    EXPECT_LT(locked_at, 800); // within slew-in (~20) + lock_samples (500) + margin
}


TEST(SoftPllTest, outlier_reject_still_guards_once_locked)
{
    int64_t const cycle = 1000000;
    SoftPll disc{nanoseconds{cycle}};

    // Drive it to lock first, so the outlier gate is armed.
    int64_t wake = 5000000;
    stepPlant(disc, wake, 0, cycle);
    for (int i = 0; i < 1000 and not disc.locked(); ++i)
    {
        stepPlant(disc, wake, 0, cycle);
    }
    ASSERT_TRUE(disc.locked());

    // A single outlier beyond the reject window must now coast (correction unchanged), proving the
    // gate re-engages after lock rather than being disabled outright.
    nanoseconds const d_outlier = disc.update(static_cast<uint64_t>(wake + 300000), false);
    EXPECT_EQ(0, d_outlier.count());
    EXPECT_TRUE(disc.locked());
}


TEST(SoftPllTest, sustained_post_lock_outlier_re_acquires)
{
    int64_t const cycle = 1000000; // 1 ms
    SoftPll disc{nanoseconds{cycle}};

    // Lock first.
    int64_t wake = 5000000;
    stepPlant(disc, wake, 0, cycle);
    for (int i = 0; i < 1000 and not disc.locked(); ++i)
    {
        stepPlant(disc, wake, 0, cycle);
    }
    ASSERT_TRUE(disc.locked());

    // The reference jumps 300 us (beyond the 150 us reject window) and stays there. The loop must
    // not reject forever with locked() stuck true: after the escape streak it abandons the stale
    // lock and re-acquires to the new phase.
    int64_t const jump = 300000;
    bool dropped  = false;
    bool relocked = false;
    for (int i = 0; i < 3000; ++i)
    {
        stepPlant(disc, wake, jump, cycle);
        if (not disc.locked())        { dropped  = true; }
        if (dropped and disc.locked()) { relocked = true; break; }
    }
    EXPECT_TRUE(dropped);   // stale lock abandoned (old behavior: never)
    EXPECT_TRUE(relocked);  // re-acquired to the moved reference
}


TEST(SoftPllTest, coast_holds_bias_without_learning)
{
    int64_t const cycle = 1000000;
    SoftPll disc{nanoseconds{cycle}};

    int64_t wake = 5000000;
    stepPlant(disc, wake, 0, cycle);
    for (int i = 0; i < 30; ++i) { stepPlant(disc, wake, 50000, cycle); } // build a nonzero correction

    uint64_t const samples_before = disc.samples();
    nanoseconds const held  = disc.coast();
    nanoseconds const held2 = disc.coast();
    EXPECT_EQ(held.count(), held2.count());      // stable: re-applies the same held correction
    EXPECT_EQ(samples_before, disc.samples());   // coast does not consume/learn a sample
}


TEST(SoftPllTest, overrun_samples_are_rejected)
{
    int64_t const cycle = 1000000;
    SoftPll disc{nanoseconds{cycle}};

    int64_t wake = 5000000;
    stepPlant(disc, wake, 0, cycle);

    // A cycle flagged as overran must coast unconditionally, even before lock: a late wake yields a
    // bogus phase sample. Correction unchanged (0 here), so no phase nudge. (The |error| outlier
    // gate is staged on lock instead -- see cold_start_* and outlier_reject_still_guards_once_locked.)
    nanoseconds const d_overrun = disc.update(static_cast<uint64_t>(wake + 50000), true);
    EXPECT_EQ(0, d_overrun.count());
}


TEST(SoftPllTest, coast_holds_the_last_nonzero_correction)
{
    int64_t const cycle = 1000000;
    SoftPll disc{nanoseconds{cycle}};

    int64_t wake = 5000000;
    stepPlant(disc, wake, 0, cycle);

    // Build up a non-zero correction with a sustained offset.
    int64_t const injected = 60000; // 60 us, under the reject threshold
    nanoseconds last_delta{0};
    for (int i = 0; i < 5; ++i)
    {
        last_delta = disc.update(static_cast<uint64_t>(wake + injected), false);
        wake += cycle + last_delta.count();
    }
    ASSERT_NE(0, last_delta.count()); // precondition: correction is non-zero

    // An overrun must re-apply that same correction (hold the frequency bias), not fall back to 0.
    nanoseconds const d_overrun = disc.update(static_cast<uint64_t>(wake + injected), true);
    EXPECT_EQ(last_delta.count(), d_overrun.count());
}


TEST(SoftPllTest, per_cycle_correction_respects_slew_limit)
{
    int64_t const cycle = 1000000;
    SoftPll::Config cfg; // default slew 20 us
    SoftPll disc{nanoseconds{cycle}, cfg};

    int64_t wake = 5000000;
    stepPlant(disc, wake, 0, cycle);

    // A large (but not rejected) step: the correction must be clamped to +/- slew each cycle.
    int64_t const injected = 140000; // under the 150 us reject threshold
    for (int i = 0; i < 50; ++i)
    {
        nanoseconds const delta = disc.update(static_cast<uint64_t>(wake + injected), false);
        EXPECT_LE(std::llabs(delta.count()), cfg.slew_limit.count());
        wake += cycle + delta.count();
    }
}


TEST(SoftPllTest, lock_has_hysteresis_and_does_not_chatter)
{
    int64_t const cycle = 1000000; // 1 ms
    SoftPll::Config cfg;
    cfg.ema_alpha        = 1.0;    // ema == last error: deterministic detector test
    cfg.lock_threshold   = 8us;
    cfg.lock_samples     = 5;
    cfg.unlock_threshold = 20us;
    cfg.unlock_samples   = 3;
    SoftPll disc{nanoseconds{cycle}, cfg};

    // Feed a phase directly (open loop): reference % cycle == phase, target learned as 0.
    auto feed = [&](int64_t phase) { disc.update(static_cast<uint64_t>(phase), false); };

    for (int i = 0; i < 6; ++i) { feed(0); }
    EXPECT_TRUE(disc.locked());

    // Excursion above the unlock threshold but shorter than unlock_samples: lock holds.
    feed(30000);
    feed(30000);
    EXPECT_TRUE(disc.locked());
    feed(0);                       // back in band resets the unlock debounce
    EXPECT_TRUE(disc.locked());

    // Sustained excursion beyond unlock_samples in a row: lock finally drops.
    feed(30000);
    feed(30000);
    feed(30000);
    EXPECT_FALSE(disc.locked());
}


TEST(SoftPllTest, acquisition_tolerates_occasional_jitter)
{
    int64_t const cycle = 1000000;
    SoftPll::Config cfg;
    cfg.ema_alpha        = 1.0;   // ema == error: deterministic
    cfg.lock_threshold   = 8us;
    cfg.lock_samples     = 5;
    cfg.unlock_threshold = 20us;
    cfg.unlock_samples   = 3;
    SoftPll disc{nanoseconds{cycle}, cfg};

    auto feed = [&](int64_t phase) { disc.update(static_cast<uint64_t>(phase), false); };

    feed(0);            // learns target 0, lock_count -> 1
    feed(0);            // 2
    feed(0);            // 3
    feed(0);            // 4
    EXPECT_FALSE(disc.locked());
    feed(30000);        // out of band: leaky counter steps back to 3 (a hard reset would go to 0)
    EXPECT_FALSE(disc.locked());
    feed(0);            // 4
    feed(0);            // 5 -> lock reached despite the excursion
    EXPECT_TRUE(disc.locked());
}
