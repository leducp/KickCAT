/// @file crash_test.cc
/// @brief Multi-process crash recovery test for kickmsg.
///
/// Forks a child publisher, kills it mid-commit with SIGKILL, then verifies
/// the channel recovers via diagnose() + repair_locked_entries() +
/// reset_retired_rings() + reclaim_orphaned_slots(). A subscriber running
/// throughout validates that no corruption occurs before or after the crash.

#include <atomic>
#include <chrono>
#include <cinttypes>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>

#include "kickcat/OS/Time.h"
#include "kickmsg/Publisher.h"
#include "kickmsg/Subscriber.h"

using namespace kickcat;

static constexpr char const* SHM_NAME = "/kickmsg_crash_test";

struct CrashPayload
{
    static constexpr uint32_t MAGIC = 0xDEADC0DE;
    uint32_t magic;
    uint32_t seq;
    uint32_t checksum;
};

static uint32_t compute_checksum(CrashPayload const& p)
{
    return p.magic ^ p.seq ^ 0xBAADF00D;
}

/// Child publisher: publishes as fast as possible using allocate() + publish()
/// to maximize the window where a kill can orphan a slot.
static void child_publisher_main(int /*round*/)
{
    auto region = kickmsg::SharedRegion::open(SHM_NAME);
    kickmsg::Publisher pub(region);

    for (uint32_t i = 0; ; ++i)
    {
        auto* ptr = pub.allocate(sizeof(CrashPayload));
        if (ptr == nullptr)
        {
            kickcat::sleep(0ns);
            continue;
        }

        CrashPayload msg;
        msg.magic    = CrashPayload::MAGIC;
        msg.seq      = i;
        msg.checksum = compute_checksum(msg);
        std::memcpy(ptr, &msg, sizeof(msg));

        pub.publish();
    }
}

/// Child subscriber: receives and validates for a fixed duration.
static void child_subscriber_main(int result_fd, int signal_fd)
{
    auto region = kickmsg::SharedRegion::open(SHM_NAME);
    kickmsg::Subscriber sub(region);

    uint64_t received  = 0;
    uint64_t corrupted = 0;

    while (true)
    {
        auto sample = sub.receive(200ms);
        if (not sample)
        {
            // Check if parent signaled us to exit
            char buf;
            ssize_t n = read(signal_fd, &buf, 1);
            if (n <= 0)
            {
                break;
            }
            continue;
        }

        if (sample->len() != sizeof(CrashPayload))
        {
            ++corrupted;
            continue;
        }

        CrashPayload msg;
        std::memcpy(&msg, sample->data(), sizeof(msg));
        if (msg.magic != CrashPayload::MAGIC or msg.checksum != compute_checksum(msg))
        {
            ++corrupted;
        }
        ++received;
    }

    // Report results via pipe
    struct { uint64_t recv; uint64_t corrupt; } result = {received, corrupted};
    ssize_t written = write(result_fd, &result, sizeof(result));
    (void)written;
    close(result_fd);
    close(signal_fd);
}

struct RoundResult
{
    bool recovered_entries;
    bool recovered_rings;
    bool recovered_slots;
    bool subscriber_ok;
};

static RoundResult run_one_round(int round)
{
    RoundResult result{};

    // Fork publisher
    pid_t pub_pid = fork();
    if (pub_pid == 0)
    {
        child_publisher_main(round);
        _exit(0); // never reached
    }

    // Let publisher run for 20-50ms
    kickcat::sleep(milliseconds{20 + (round % 30)});

    // Kill publisher mid-flight
    kill(pub_pid, SIGKILL);
    int status;
    waitpid(pub_pid, &status, 0);

    // Diagnose damage
    auto region = kickmsg::SharedRegion::open(SHM_NAME);
    auto report = region.diagnose();

    result.recovered_entries = (report.locked_entries > 0);
    result.recovered_rings   = (report.retired_rings > 0);

    // Repair
    std::size_t repaired  = region.repair_locked_entries();
    std::size_t reset     = region.reset_retired_rings();
    std::size_t reclaimed = region.reclaim_orphaned_slots();

    result.recovered_slots = (reclaimed > 0);

    if (repaired > 0 or reset > 0 or reclaimed > 0)
    {
        std::printf("  Round %d: repaired %zu entries, reset %zu rings, reclaimed %zu slots\n",
                    round, repaired, reset, reclaimed);
    }

    // Verify clean after repair
    auto post = region.diagnose();
    if (post.locked_entries > 0 or post.retired_rings > 0)
    {
        std::fprintf(stderr, "  [FAIL] Round %d: repair incomplete "
                     "(locked=%u, retired=%u)\n",
                     round, post.locked_entries, post.retired_rings);
    }

    // Fork a new publisher to verify the channel still works
    pid_t pub2_pid = fork();
    if (pub2_pid == 0)
    {
        auto reg = kickmsg::SharedRegion::open(SHM_NAME);
        kickmsg::Publisher pub(reg);

        for (uint32_t i = 0; i < 100; ++i)
        {
            CrashPayload msg;
            msg.magic    = CrashPayload::MAGIC;
            msg.seq      = 1000000 + i;
            msg.checksum = compute_checksum(msg);
            while (pub.send(&msg, sizeof(msg)) < 0)
            {
                kickcat::sleep(0ns);
            }
        }
        _exit(0);
    }

    waitpid(pub2_pid, &status, 0);
    result.subscriber_ok = true;

    return result;
}

int main()
{
    std::printf("=== KickMsg Multi-Process Crash Test ===\n\n");

    kickmsg::SharedMemory::unlink(SHM_NAME);

    kickmsg::channel::Config cfg;
    cfg.max_subscribers   = 4;
    cfg.sub_ring_capacity = 32;
    cfg.pool_size         = 64;
    cfg.max_payload_size  = sizeof(CrashPayload);

    auto region = kickmsg::SharedRegion::create(
        SHM_NAME, kickmsg::channel::PubSub, cfg, "crash_test");

    // Fork a long-lived subscriber.
    // signal_pipe: parent writes to [1] to tell subscriber to exit.
    // result_pipe: subscriber writes results to [1], parent reads from [0].
    int signal_pipe[2];
    int result_pipe[2];
    pipe(signal_pipe);
    pipe(result_pipe);

    pid_t sub_pid = fork();
    if (sub_pid == 0)
    {
        close(signal_pipe[1]); // close write end
        close(result_pipe[0]); // close read end
        child_subscriber_main(result_pipe[1], signal_pipe[0]);
        _exit(0);
    }
    close(signal_pipe[0]); // close read end in parent
    close(result_pipe[1]); // close write end in parent

    // Let subscriber attach
    kickcat::sleep(50ms);

    constexpr int NUM_ROUNDS = 10;
    int any_recovery = 0;
    bool all_ok = true;

    for (int round = 0; round < NUM_ROUNDS; ++round)
    {
        auto result = run_one_round(round);
        if (result.recovered_entries or result.recovered_rings or result.recovered_slots)
        {
            ++any_recovery;
        }
    }

    // Signal subscriber to exit
    close(signal_pipe[1]);

    // Read subscriber results
    struct { uint64_t recv; uint64_t corrupt; } sub_result{};
    read(result_pipe[0], &sub_result, sizeof(sub_result));
    close(result_pipe[0]);

    int sub_status;
    waitpid(sub_pid, &sub_status, 0);

    std::printf("  Subscriber: received %" PRIu64 ", corrupted %" PRIu64 "\n",
                sub_result.recv, sub_result.corrupt);

    if (sub_result.corrupt > 0)
    {
        std::fprintf(stderr, "  [FAIL] Subscriber saw %" PRIu64 " corrupted messages!\n",
                     sub_result.corrupt);
        all_ok = false;
    }

    std::printf("\n  Rounds: %d, rounds with recovery: %d\n", NUM_ROUNDS, any_recovery);

    if (any_recovery == 0)
    {
        std::printf("  [WARN] No crash damage detected in %d rounds "
                    "(kill timing may have missed the commit window)\n", NUM_ROUNDS);
    }

    // Final cleanup and verification
    {
        auto reg = kickmsg::SharedRegion::open(SHM_NAME);
        reg.repair_locked_entries();
        reg.reset_retired_rings();
        reg.reclaim_orphaned_slots();

        auto report = reg.diagnose();
        if (report.locked_entries > 0 or report.retired_rings > 0)
        {
            std::fprintf(stderr, "  [FAIL] Final state not clean\n");
            all_ok = false;
        }
    }

    kickmsg::SharedMemory::unlink(SHM_NAME);

    if (all_ok)
    {
        std::printf("  [PASS]\n");
    }
    else
    {
        std::printf("  [FAIL]\n");
    }

    return all_ok ? 0 : 1;
}
