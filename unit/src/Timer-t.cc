// Timer owning the soft PLL: verify it drives the owned SoftPll and exposes lock/diagnostics.
// The control loop itself is covered by SoftPll-t.cc; here we only check the wiring, so no real
// sleeping is needed -- sync_to advances the PLL without touching wait_next_tick.
#include <gtest/gtest.h>

#include <cstdint>

#include "kickcat/OS/Timer.h"

using namespace kickcat;


TEST(TimerPllTest, sync_to_advances_the_owned_pll)
{
    int64_t const cycle = 1000000; // 1 ms
    Timer timer{nanoseconds{cycle}};

    EXPECT_EQ(0u, timer.pll().samples());

    // A constant reference phase keeps the measured error at the learned target: the PLL stays
    // quiet and eventually declares lock after lock_samples in-threshold cycles.
    int64_t ref = 5000000;
    for (uint32_t i = 0; i < SoftPll::Config{}.lock_samples; ++i)
    {
        timer.sync_to(static_cast<uint64_t>(ref));
        ref += cycle;
    }

    EXPECT_EQ(SoftPll::Config{}.lock_samples, timer.pll().samples());
    EXPECT_EQ(0, timer.pll().phase_error().count());
    EXPECT_TRUE(timer.locked());
}


TEST(TimerPllTest, update_period_rebuilds_the_pll_on_the_new_grid)
{
    Timer timer{1ms};

    timer.sync_to(5000000);
    ASSERT_EQ(1u, timer.pll().samples());

    // Changing the period invalidates the learned target phase: the PLL must start fresh.
    timer.update_period(2ms);
    EXPECT_EQ(0u, timer.pll().samples());
    EXPECT_FALSE(timer.locked());
}
