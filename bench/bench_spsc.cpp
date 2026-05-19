#include <lob/spsc_ring.hpp>

#include <benchmark/benchmark.h>

#include <atomic>
#include <cstdint>
#include <thread>

namespace {

constexpr std::size_t default_capacity = 1U << 16;

void bench_push_pop_single_threaded(benchmark::State& state) {
    lob::spsc_ring<std::uint64_t, default_capacity> ring;
    std::uint64_t v{};
    for (auto _ : state) {
        benchmark::DoNotOptimize(ring.try_push(v));
        benchmark::DoNotOptimize(ring.try_pop(v));
    }
    state.SetItemsProcessed(state.iterations());
}
BENCHMARK(bench_push_pop_single_threaded);

void bench_burst_then_drain(benchmark::State& state) {
    lob::spsc_ring<std::uint64_t, default_capacity> ring;
    const auto n = static_cast<std::size_t>(state.range(0));
    for (auto _ : state) {
        for (std::uint64_t i = 0; i < n; ++i) benchmark::DoNotOptimize(ring.try_push(i));
        std::uint64_t out{};
        while (ring.try_pop(out)) benchmark::DoNotOptimize(out);
    }
    state.SetItemsProcessed(state.iterations() * n);
}
BENCHMARK(bench_burst_then_drain)->Range(64, 32'768);

void bench_producer_consumer_throughput(benchmark::State& state) {
    constexpr std::size_t cap = 4096;
    static lob::spsc_ring<std::uint64_t, cap> ring;
    static std::atomic<std::uint64_t> produced{0};
    static std::atomic<std::uint64_t> consumed{0};
    static std::atomic<bool>          stop{false};
    static std::thread                producer;
    static std::thread                consumer;
    static std::atomic<bool>          spun{false};

    if (state.thread_index() == 0 && !spun.exchange(true, std::memory_order_acq_rel)) {
        producer = std::thread{[] {
            std::uint64_t i = 0;
            while (!stop.load(std::memory_order_acquire)) {
                if (ring.try_push(i)) {
                    ++i;
                    produced.store(i, std::memory_order_release);
                }
            }
        }};
        consumer = std::thread{[] {
            std::uint64_t out{};
            while (!stop.load(std::memory_order_acquire)) {
                if (ring.try_pop(out)) consumed.fetch_add(1, std::memory_order_relaxed);
            }
        }};
    }

    for (auto _ : state) {
        const auto before = consumed.load(std::memory_order_relaxed);
        while (consumed.load(std::memory_order_relaxed) - before < 1024) {
            // spin until the consumer has drained another batch
        }
    }

    if (state.thread_index() == 0) {
        state.SetItemsProcessed(static_cast<int64_t>(consumed.load(std::memory_order_relaxed)));
        stop.store(true, std::memory_order_release);
        if (producer.joinable()) producer.join();
        if (consumer.joinable()) consumer.join();
    }
}
BENCHMARK(bench_producer_consumer_throughput)->UseRealTime()->MinTime(0.5);

}  // namespace
