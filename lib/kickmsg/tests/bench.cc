/// @file bench.cc
/// @brief Latency and throughput benchmark for kickmsg.
///
/// Measures publish-to-receive latency (p50/p90/p99/p999/max) and
/// throughput (msgs/sec, MB/sec) across a range of payload sizes:
///   Small:  64B, 256B, 1K, 4K
///   Medium: 128K, 256K, 512K
///   Large:  1M, 4M, 16M

#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

#include "kickcat/OS/Time.h"
#include "kickmsg/Publisher.h"
#include "kickmsg/Subscriber.h"

using namespace kickcat;

static constexpr char const* SHM_NAME = "/kickmsg_bench";

struct BenchConfig
{
    std::size_t payload_bytes;
    uint32_t    num_msgs;
    std::size_t pool_size;
    std::size_t ring_capacity;
};

static BenchConfig make_config(std::size_t payload_bytes)
{
    BenchConfig bc;
    bc.payload_bytes = payload_bytes;

    // Scale message count inversely with payload size
    if (payload_bytes <= 4096)
    {
        bc.num_msgs = 100000;
    }
    else if (payload_bytes <= 512 * 1024)
    {
        bc.num_msgs = 10000;
    }
    else
    {
        bc.num_msgs = 1000;
    }

    // Scale pool/ring to avoid exhaustion without wasting memory
    if (payload_bytes <= 4096)
    {
        bc.pool_size     = 1024;
        bc.ring_capacity = 512;
    }
    else if (payload_bytes <= 512 * 1024)
    {
        bc.pool_size     = 128;
        bc.ring_capacity = 64;
    }
    else
    {
        bc.pool_size     = 16;
        bc.ring_capacity = 8;
    }

    return bc;
}

struct LatencyResult
{
    uint64_t p50;
    uint64_t p90;
    uint64_t p99;
    uint64_t p999;
    uint64_t max;
};

static LatencyResult compute_percentiles(std::vector<uint64_t>& samples)
{
    std::sort(samples.begin(), samples.end());

    std::size_t n = samples.size();
    LatencyResult r;
    r.p50  = samples[n * 50 / 100];
    r.p90  = samples[n * 90 / 100];
    r.p99  = samples[n * 99 / 100];
    r.p999 = samples[std::min(n - 1, n * 999 / 1000)];
    r.max  = samples[n - 1];
    return r;
}

static void run_latency(BenchConfig const& bc, bool zerocopy)
{
    kickmsg::SharedMemory::unlink(SHM_NAME);

    kickmsg::channel::Config cfg;
    cfg.max_subscribers   = 2;
    cfg.sub_ring_capacity = bc.ring_capacity;
    cfg.pool_size         = bc.pool_size;
    cfg.max_payload_size  = bc.payload_bytes;

    auto region = kickmsg::SharedRegion::create(
        SHM_NAME, kickmsg::channel::PubSub, cfg, "bench");

    kickmsg::Subscriber sub(region);
    kickmsg::Publisher  pub(region);

    std::vector<uint64_t> latencies;
    latencies.reserve(bc.num_msgs);

    std::vector<uint8_t> payload(bc.payload_bytes, 0);

    // Warmup
    for (uint32_t i = 0; i < 100 and i < bc.num_msgs; ++i)
    {
        nanoseconds ts = kickcat::since_epoch();
        std::memcpy(payload.data(), &ts, sizeof(ts));
        pub.send(payload.data(), payload.size());

        if (zerocopy)
        {
            auto view = sub.try_receive_view();
        }
        else
        {
            auto sample = sub.try_receive();
        }
    }

    // Measured run
    for (uint32_t i = 0; i < bc.num_msgs; ++i)
    {
        nanoseconds send_ts = kickcat::since_epoch();
        std::memcpy(payload.data(), &send_ts, sizeof(send_ts));

        while (pub.send(payload.data(), payload.size()) < 0)
        {
            kickcat::sleep(0ns);
        }

        if (zerocopy)
        {
            while (true)
            {
                auto view = sub.try_receive_view();
                if (view)
                {
                    nanoseconds recv_ts = kickcat::since_epoch();
                    nanoseconds sent;
                    std::memcpy(&sent, view->data(), sizeof(sent));
                    uint64_t lat = static_cast<uint64_t>((recv_ts - sent).count());
                    latencies.push_back(lat);
                    break;
                }
            }
        }
        else
        {
            while (true)
            {
                auto sample = sub.try_receive();
                if (sample)
                {
                    nanoseconds recv_ts = kickcat::since_epoch();
                    nanoseconds sent;
                    std::memcpy(&sent, sample->data(), sizeof(sent));
                    uint64_t lat = static_cast<uint64_t>((recv_ts - sent).count());
                    latencies.push_back(lat);
                    break;
                }
            }
        }
    }

    auto r = compute_percentiles(latencies);

    char const* mode = zerocopy ? "zerocopy" : "copy";
    std::printf("%-10zu %-10s %-5d %-5d %-10" PRIu64 " %-10" PRIu64
                " %-10" PRIu64 " %-10" PRIu64 " %-10" PRIu64
                " %-12s %-12s\n",
                bc.payload_bytes, mode, 1, 1,
                r.p50, r.p90, r.p99, r.p999, r.max,
                "-", "-");

    region.unlink();
}

static void run_throughput(BenchConfig const& bc, bool zerocopy,
                           int num_pubs, int num_subs)
{
    kickmsg::SharedMemory::unlink(SHM_NAME);

    kickmsg::channel::Config cfg;
    cfg.max_subscribers   = static_cast<std::size_t>(num_subs + 2);
    cfg.sub_ring_capacity = bc.ring_capacity;
    cfg.pool_size         = bc.pool_size;
    cfg.max_payload_size  = bc.payload_bytes;

    auto region = kickmsg::SharedRegion::create(
        SHM_NAME, kickmsg::channel::PubSub, cfg, "bench");

    std::atomic<bool> done{false};
    std::atomic<uint64_t> total_received{0};

    std::vector<uint8_t> payload(bc.payload_bytes, 0xAB);

    // Subscribers
    std::vector<std::thread> sub_threads;
    for (int i = 0; i < num_subs; ++i)
    {
        sub_threads.emplace_back([&region, &done, &total_received, zerocopy]()
        {
            kickmsg::Subscriber sub(region);
            uint64_t count = 0;

            while (not done.load(std::memory_order_relaxed))
            {
                if (zerocopy)
                {
                    auto view = sub.try_receive_view();
                    if (view)
                    {
                        ++count;
                    }
                }
                else
                {
                    auto sample = sub.try_receive();
                    if (sample)
                    {
                        ++count;
                    }
                }
            }

            // Drain remaining
            while (true)
            {
                bool got = false;
                if (zerocopy)
                {
                    got = sub.try_receive_view().has_value();
                }
                else
                {
                    got = sub.try_receive().has_value();
                }
                if (not got)
                {
                    break;
                }
                ++count;
            }

            total_received.fetch_add(count, std::memory_order_relaxed);
        });
    }

    kickcat::sleep(10ms);

    // Publishers
    std::atomic<uint64_t> total_sent{0};
    std::vector<std::thread> pub_threads;
    for (int i = 0; i < num_pubs; ++i)
    {
        pub_threads.emplace_back([&region, &done, &total_sent, &payload]()
        {
            kickmsg::Publisher pub(region);
            uint64_t count = 0;

            while (not done.load(std::memory_order_relaxed))
            {
                if (pub.send(payload.data(), payload.size()) >= 0)
                {
                    ++count;
                }
            }

            total_sent.fetch_add(count, std::memory_order_relaxed);
        });
    }

    // Run for 2 seconds
    nanoseconds start = kickcat::since_epoch();
    kickcat::sleep(seconds{2});
    done.store(true, std::memory_order_relaxed);

    for (auto& t : pub_threads)
    {
        t.join();
    }
    for (auto& t : sub_threads)
    {
        t.join();
    }

    nanoseconds elapsed = kickcat::elapsed_time(start);
    double elapsed_sec = std::chrono::duration<double>(elapsed).count();

    uint64_t sent = total_sent.load();

    double msgs_sec = static_cast<double>(sent) / elapsed_sec;
    double mb_sec   = (static_cast<double>(sent) * static_cast<double>(bc.payload_bytes))
                      / (elapsed_sec * 1024.0 * 1024.0);

    char msgs_buf[32];
    char mb_buf[32];
    std::snprintf(msgs_buf, sizeof(msgs_buf), "%.0f", msgs_sec);
    std::snprintf(mb_buf, sizeof(mb_buf), "%.1f", mb_sec);

    char const* mode = zerocopy ? "zerocopy" : "copy";
    std::printf("%-10zu %-10s %-5d %-5d %-10s %-10s %-10s %-10s %-10s %-12s %-12s\n",
                bc.payload_bytes, mode, num_pubs, num_subs,
                "-", "-", "-", "-", "-",
                msgs_buf, mb_buf);

    region.unlink();
}

int main()
{
    std::printf("=== KickMsg Benchmark ===\n\n");

    std::vector<std::size_t> payload_sizes = {
        64, 256, 1024, 4096,                        // small
        128 * 1024, 256 * 1024, 512 * 1024,         // medium
        1024 * 1024, 4 * 1024 * 1024, 16 * 1024 * 1024  // large
    };

    std::printf("%-10s %-10s %-5s %-5s %-10s %-10s %-10s %-10s %-10s %-12s %-12s\n",
                "payload", "mode", "pubs", "subs",
                "p50_ns", "p90_ns", "p99_ns", "p999_ns", "max_ns",
                "msgs/sec", "MB/sec");
    std::printf("%-10s %-10s %-5s %-5s %-10s %-10s %-10s %-10s %-10s %-12s %-12s\n",
                "-------", "--------", "----", "----",
                "------", "------", "------", "-------", "------",
                "--------", "------");

    // Latency benchmarks: 1P/1S
    for (auto sz : payload_sizes)
    {
        auto bc = make_config(sz);
        run_latency(bc, false);
        run_latency(bc, true);
    }

    std::printf("\n");

    // Throughput benchmarks: 4P/8S
    for (auto sz : payload_sizes)
    {
        auto bc = make_config(sz);
        run_throughput(bc, false, 4, 8);
        run_throughput(bc, true, 4, 8);
    }

    std::printf("\nDone.\n");
    return 0;
}
