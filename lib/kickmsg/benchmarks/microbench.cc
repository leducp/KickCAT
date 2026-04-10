/// @file microbench.cc
/// @brief Google Benchmark microbenchmarks for kickmsg internals.
///
/// Isolates individual operations to measure their cost:
///   - Treiber stack pop/push
///   - Publish hot path (single ring, no contention)
///   - Receive hot path (copy vs zerocopy)
///   - Packed state_flight CAS admission
///   - Futex conditional wake overhead

#include <benchmark/benchmark.h>

#include <cstring>
#include <memory>

#include "kickcat/OS/Time.h"
#include "kickmsg/Publisher.h"
#include "kickmsg/Subscriber.h"

using namespace kickcat;

static constexpr char const* SHM_NAME = "/kickmsg_microbench";

// --- Treiber stack ---

static void BM_TreiberPopPush(benchmark::State& state)
{
    kickmsg::SharedMemory::unlink(SHM_NAME);

    kickmsg::channel::Config cfg;
    cfg.max_subscribers   = 1;
    cfg.sub_ring_capacity = 4;
    cfg.pool_size         = 64;
    cfg.max_payload_size  = 64;

    auto region = kickmsg::SharedRegion::create(
        SHM_NAME, kickmsg::channel::PubSub, cfg, "bench");

    auto* base = region.base();
    auto* hdr  = region.header();

    for (auto _ : state)
    {
        uint32_t idx = kickmsg::treiber_pop(hdr->free_top, base, hdr);
        benchmark::DoNotOptimize(idx);
        if (idx != kickmsg::INVALID_SLOT)
        {
            auto* slot = kickmsg::slot_at(base, hdr, idx);
            kickmsg::treiber_push(hdr->free_top, slot, idx);
        }
    }

    region.unlink();
}
BENCHMARK(BM_TreiberPopPush);

// --- Publish + receive (1P/1S, copy) ---

static void BM_SendReceiveCopy(benchmark::State& state)
{
    std::size_t payload_size = static_cast<std::size_t>(state.range(0));

    kickmsg::SharedMemory::unlink(SHM_NAME);

    kickmsg::channel::Config cfg;
    cfg.max_subscribers   = 2;
    cfg.sub_ring_capacity = 1024;
    cfg.pool_size         = 2048;
    cfg.max_payload_size  = payload_size;

    auto region = kickmsg::SharedRegion::create(
        SHM_NAME, kickmsg::channel::PubSub, cfg, "bench");

    kickmsg::Subscriber sub(region);
    kickmsg::Publisher  pub(region);

    std::vector<uint8_t> payload(payload_size, 0xAB);

    for (auto _ : state)
    {
        pub.send(payload.data(), payload.size());
        auto sample = sub.try_receive();
        benchmark::DoNotOptimize(sample);
    }

    state.SetBytesProcessed(
        static_cast<int64_t>(state.iterations()) *
        static_cast<int64_t>(payload_size));

    region.unlink();
}
BENCHMARK(BM_SendReceiveCopy)
    ->Arg(64)->Arg(256)->Arg(1024)->Arg(4096)
    ->Arg(128 * 1024)->Arg(1024 * 1024);

// --- Publish + receive (1P/1S, zerocopy) ---

static void BM_SendReceiveZerocopy(benchmark::State& state)
{
    std::size_t payload_size = static_cast<std::size_t>(state.range(0));

    kickmsg::SharedMemory::unlink(SHM_NAME);

    kickmsg::channel::Config cfg;
    cfg.max_subscribers   = 2;
    cfg.sub_ring_capacity = 1024;
    cfg.pool_size         = 2048;
    cfg.max_payload_size  = payload_size;

    auto region = kickmsg::SharedRegion::create(
        SHM_NAME, kickmsg::channel::PubSub, cfg, "bench");

    kickmsg::Subscriber sub(region);
    kickmsg::Publisher  pub(region);

    std::vector<uint8_t> payload(payload_size, 0xAB);

    for (auto _ : state)
    {
        pub.send(payload.data(), payload.size());
        auto view = sub.try_receive_view();
        benchmark::DoNotOptimize(view);
    }

    state.SetBytesProcessed(
        static_cast<int64_t>(state.iterations()) *
        static_cast<int64_t>(payload_size));

    region.unlink();
}
BENCHMARK(BM_SendReceiveZerocopy)
    ->Arg(64)->Arg(256)->Arg(1024)->Arg(4096)
    ->Arg(128 * 1024)->Arg(1024 * 1024);

// --- Publish only (no subscriber, measures raw publish cost) ---

static void BM_PublishOnly(benchmark::State& state)
{
    kickmsg::SharedMemory::unlink(SHM_NAME);

    kickmsg::channel::Config cfg;
    cfg.max_subscribers   = 4;
    cfg.sub_ring_capacity = 1024;
    cfg.pool_size         = 2048;
    cfg.max_payload_size  = 64;

    auto region = kickmsg::SharedRegion::create(
        SHM_NAME, kickmsg::channel::PubSub, cfg, "bench");

    kickmsg::Publisher pub(region);

    uint64_t val = 0;
    for (auto _ : state)
    {
        // No subscribers: all rings are Free, publisher skips them all
        // via relaxed pre-check. Measures: allocate + batch excess only.
        pub.send(&val, sizeof(val));
        ++val;
    }

    region.unlink();
}
BENCHMARK(BM_PublishOnly);

// --- Publish with 1 active subscriber (measures per-ring cost) ---

static void BM_PublishOneSubscriber(benchmark::State& state)
{
    kickmsg::SharedMemory::unlink(SHM_NAME);

    kickmsg::channel::Config cfg;
    cfg.max_subscribers   = 4;
    cfg.sub_ring_capacity = 1024;
    cfg.pool_size         = 2048;
    cfg.max_payload_size  = 64;

    auto region = kickmsg::SharedRegion::create(
        SHM_NAME, kickmsg::channel::PubSub, cfg, "bench");

    kickmsg::Subscriber sub(region);
    kickmsg::Publisher  pub(region);

    uint64_t val = 0;
    for (auto _ : state)
    {
        pub.send(&val, sizeof(val));
        ++val;
        // Don't receive — let the ring overflow and evict.
        // Measures: pre-check + CAS admission + fetch_add +
        // two-phase commit + eviction + batch excess.
    }

    region.unlink();
}
BENCHMARK(BM_PublishOneSubscriber);

// --- Publish with N active subscribers (measures scaling) ---

static void BM_PublishNSubscribers(benchmark::State& state)
{
    int num_subs = static_cast<int>(state.range(0));

    kickmsg::SharedMemory::unlink(SHM_NAME);

    kickmsg::channel::Config cfg;
    cfg.max_subscribers   = 16;
    cfg.sub_ring_capacity = 1024;
    cfg.pool_size         = 2048;
    cfg.max_payload_size  = 64;

    auto region = kickmsg::SharedRegion::create(
        SHM_NAME, kickmsg::channel::PubSub, cfg, "bench");

    std::vector<std::unique_ptr<kickmsg::Subscriber>> subs;
    for (int i = 0; i < num_subs; ++i)
    {
        subs.push_back(std::make_unique<kickmsg::Subscriber>(region));
    }

    kickmsg::Publisher pub(region);

    uint64_t val = 0;
    for (auto _ : state)
    {
        pub.send(&val, sizeof(val));
        ++val;
    }

    state.counters["subs"] = static_cast<double>(num_subs);

    subs.clear();
    region.unlink();
}
BENCHMARK(BM_PublishNSubscribers)
    ->Arg(1)->Arg(2)->Arg(4)->Arg(8)->Arg(10)->Arg(16);

// --- CAS admission cost (packed state_flight) ---

static void BM_CASAdmission(benchmark::State& state)
{
    kickmsg::SharedMemory::unlink(SHM_NAME);

    kickmsg::channel::Config cfg;
    cfg.max_subscribers   = 1;
    cfg.sub_ring_capacity = 4;
    cfg.pool_size         = 8;
    cfg.max_payload_size  = 8;

    auto region = kickmsg::SharedRegion::create(
        SHM_NAME, kickmsg::channel::PubSub, cfg, "bench");

    auto* ring = kickmsg::sub_ring_at(region.base(), region.header(), 0);
    // Set ring to Live so CAS admission succeeds
    ring->state_flight.store(
        kickmsg::ring::make_packed(kickmsg::ring::Live),
        std::memory_order_release);

    for (auto _ : state)
    {
        // CAS: increment in_flight
        uint32_t old = ring->state_flight.load(std::memory_order_acquire);
        ring->state_flight.compare_exchange_strong(
            old, old + kickmsg::ring::IN_FLIGHT_ONE,
            std::memory_order_acq_rel, std::memory_order_acquire);
        benchmark::ClobberMemory();

        // Undo: decrement in_flight
        ring->state_flight.fetch_sub(
            kickmsg::ring::IN_FLIGHT_ONE, std::memory_order_release);
    }

    region.unlink();
}
BENCHMARK(BM_CASAdmission);

BENCHMARK_MAIN();
