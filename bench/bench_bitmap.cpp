#include <lob/bitmap.hpp>

#include <benchmark/benchmark.h>

#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

namespace {

constexpr std::size_t default_capacity = 1U << 20;

std::vector<std::size_t> sample_bits(std::size_t n, std::size_t cap, std::uint64_t seed = 0xC0FFEE) {
    std::mt19937_64 rng{seed};
    std::uniform_int_distribution<std::size_t> dist{0, cap - 1};
    std::vector<std::size_t> out;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i) out.push_back(dist(rng));
    return out;
}

void bench_set(benchmark::State& state) {
    lob::hier_bitmap<default_capacity> bm;
    const auto bits = sample_bits(static_cast<std::size_t>(state.range(0)), default_capacity);
    for (auto _ : state) {
        for (auto b : bits) bm.set(b);
        benchmark::DoNotOptimize(bm);
        state.PauseTiming();
        bm.clear_all();
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * bits.size());
}
BENCHMARK(bench_set)->Range(64, 65'536);

void bench_clear(benchmark::State& state) {
    lob::hier_bitmap<default_capacity> bm;
    const auto bits = sample_bits(static_cast<std::size_t>(state.range(0)), default_capacity);
    for (auto _ : state) {
        state.PauseTiming();
        for (auto b : bits) bm.set(b);
        state.ResumeTiming();
        for (auto b : bits) bm.clear(b);
        benchmark::DoNotOptimize(bm);
    }
    state.SetItemsProcessed(state.iterations() * bits.size());
}
BENCHMARK(bench_clear)->Range(64, 65'536);

void bench_lowest_set(benchmark::State& state) {
    lob::hier_bitmap<default_capacity> bm;
    const auto bits = sample_bits(static_cast<std::size_t>(state.range(0)), default_capacity);
    for (auto b : bits) bm.set(b);
    for (auto _ : state) {
        auto v = bm.lowest_set();
        benchmark::DoNotOptimize(v);
    }
}
BENCHMARK(bench_lowest_set)->Range(64, 65'536);

void bench_highest_set(benchmark::State& state) {
    lob::hier_bitmap<default_capacity> bm;
    const auto bits = sample_bits(static_cast<std::size_t>(state.range(0)), default_capacity);
    for (auto b : bits) bm.set(b);
    for (auto _ : state) {
        auto v = bm.highest_set();
        benchmark::DoNotOptimize(v);
    }
}
BENCHMARK(bench_highest_set)->Range(64, 65'536);

void bench_set_clear_mixed(benchmark::State& state) {
    lob::hier_bitmap<default_capacity> bm;
    const auto bits = sample_bits(static_cast<std::size_t>(state.range(0)), default_capacity);
    std::size_t i = 0;
    for (auto _ : state) {
        const auto b = bits[i++ % bits.size()];
        bm.set(b);
        benchmark::DoNotOptimize(bm.lowest_set());
        bm.clear(b);
        benchmark::ClobberMemory();
    }
}
BENCHMARK(bench_set_clear_mixed)->Range(64, 65'536);

}  // namespace
