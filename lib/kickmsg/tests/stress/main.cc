#include "common.h"

// Forward declarations
bool run_treiber_stress();
bool run_subscriber_churn();
bool run_gc_recovery();
bool run_fairness_test();
void run_all_mpmc(TestRunner& runner);
bool run_pool_exhaustion();
bool run_live_repair();
bool run_single_slot_ring();
bool run_subscriber_saturation();

int main()
{
    std::printf("=== KickMsg Lock-Free Stress Tests ===\n\n");

    TestRunner runner;

    runner.run(run_treiber_stress());
    runner.run(run_subscriber_churn());
    runner.run(run_gc_recovery());
    runner.run(run_fairness_test());
    run_all_mpmc(runner);
    runner.run(run_pool_exhaustion());
    runner.run(run_live_repair());
    runner.run(run_single_slot_ring());
    runner.run(run_subscriber_saturation());

    return runner.summary();
}
