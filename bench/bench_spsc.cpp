#include <lob/messages.hpp>
#include <lob/spsc_ring.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <thread>

#include <benchmark/benchmark.h>

namespace {

constexpr std::size_t default_capacity = 1U << 16;

void bench_burst_then_drain(benchmark::State& state) {
    lob::spsc_ring<std::uint64_t, default_capacity> ring;
    const auto n = static_cast<std::size_t>(state.range(0));
    for (auto _ : state) {
        for (std::uint64_t i = 0; i < n; ++i)
            benchmark::DoNotOptimize(ring.try_push(i));
        std::uint64_t out{};
        while (ring.try_pop(out))
            benchmark::DoNotOptimize(out);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(n));
}

BENCHMARK(bench_burst_then_drain)->Range(64, 32'768)->MinTime(0.1);

// Producer + consumer threads on a fresh ring per invocation. No static
// state survives between Google Benchmark calls; warmup and measurement
// each get a clean run.
void bench_producer_consumer(benchmark::State& state) {
    using ring_t = lob::spsc_ring<std::uint64_t, 4096>;
    ring_t ring;
    std::atomic<bool> stop{false};
    std::atomic<std::uint64_t> consumed{0};

    std::thread producer{[&] {
        std::uint64_t i = 0;
        while (!stop.load(std::memory_order_acquire)) {
            if (ring.try_push(i))
                ++i;
        }
    }};

    std::thread consumer{[&] {
        std::uint64_t out{};
        while (!stop.load(std::memory_order_acquire)) {
            if (ring.try_pop(out))
                consumed.fetch_add(1, std::memory_order_relaxed);
        }
    }};

    for (auto _ : state) {
        const auto before = consumed.load(std::memory_order_relaxed);
        while (consumed.load(std::memory_order_relaxed) - before < 1024) {
            // spin until the consumer drains another batch of 1024
        }
    }

    stop.store(true, std::memory_order_release);
    producer.join();
    consumer.join();
    state.SetItemsProcessed(static_cast<std::int64_t>(consumed.load(std::memory_order_relaxed)));
}

BENCHMARK(bench_producer_consumer)->UseRealTime()->MinTime(0.5);

// One command out on one ring and back on another, the handoff every order pays
// twice on its way through the runtime, ingress to the worker and egress to the
// merger. An iteration is one round trip, so half of it is the one-way latency
// of a ring with nothing queued.
void bench_round_trip(benchmark::State& state) {
    using ring_t = lob::spsc_ring<lob::command, 1024>;
    ring_t out;
    ring_t back;
    std::atomic<bool> stop{false};

    std::thread echo{[&] {
        lob::command c;
        while (!stop.load(std::memory_order_relaxed)) {
            if (out.try_pop(c)) {
                while (!back.try_push(c)) {
                }
            }
        }
    }};

    const lob::command sent = lob::command::make_cancel({.id = 1});
    lob::command got;
    for (auto _ : state) {
        while (!out.try_push(sent)) {
        }
        while (!back.try_pop(got)) {
        }
        benchmark::DoNotOptimize(got);
    }

    stop.store(true, std::memory_order_relaxed);
    echo.join();
}

BENCHMARK(bench_round_trip)->UseRealTime()->MinTime(0.5);

}  // namespace
