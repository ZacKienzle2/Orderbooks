#include <lob/latency_histogram.hpp>

#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

#include <benchmark/benchmark.h>

namespace {

constexpr std::uint64_t highest = 1'000'000'000;  // 1 second in nanoseconds
constexpr unsigned sig_figs = 3;

void bench_record_fixed(benchmark::State& state) {
    lob::latency_histogram hist{highest, sig_figs};
    const std::uint64_t value = 487;
    for (auto _ : state) {
        hist.record(value);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(bench_record_fixed);

void bench_record_varied(benchmark::State& state) {
    lob::latency_histogram hist{highest, sig_figs};
    std::mt19937_64 rng{0xC0FFEEULL};
    std::uniform_int_distribution<std::uint64_t> dist{1, 1'000'000};
    std::vector<std::uint64_t> samples(1024);
    for (auto& s : samples) {
        s = dist(rng);
    }
    std::size_t i = 0;
    for (auto _ : state) {
        hist.record(samples[i & 1023U]);
        ++i;
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(bench_record_varied);

void bench_value_at_percentile(benchmark::State& state) {
    lob::latency_histogram hist{highest, sig_figs};
    std::mt19937_64 rng{0xBADC0DEULL};
    std::uniform_int_distribution<std::uint64_t> dist{1, 1'000'000};
    for (std::size_t i = 0; i < 1'000'000; ++i) {
        hist.record(dist(rng));
    }
    for (auto _ : state) {
        benchmark::DoNotOptimize(hist.value_at_percentile(99.9));
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(bench_value_at_percentile);

}  // namespace
