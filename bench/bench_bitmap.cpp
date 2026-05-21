#include <lob/bitmap.hpp>

#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

#include <benchmark/benchmark.h>

namespace {

constexpr std::size_t default_capacity = 1U << 20;

std::vector<std::size_t>
sample_bits(std::size_t n, std::size_t cap, std::uint64_t seed = 0xC0FFEE) {
    std::mt19937_64 rng{seed};
    std::uniform_int_distribution<std::size_t> dist{0, cap - 1};
    std::vector<std::size_t> out;
    out.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
        out.push_back(dist(rng));
    return out;
}

// Each iteration sets `n` bits and clears the bitmap. The clear is O(tier
// count) - effectively a constant against the n sets - so the headline
// number tracks the cost of set(). Avoids the PauseTiming / ResumeTiming
// dance, which added a measurable per-iteration syscall and skewed small-n
// readings.
void bench_set(benchmark::State& state) {
    lob::hier_bitmap<default_capacity> bm;
    const auto bits = sample_bits(static_cast<std::size_t>(state.range(0)), default_capacity);
    for (auto _ : state) {
        for (auto b : bits)
            bm.set(b);
        benchmark::DoNotOptimize(bm);
        bm.clear_all();
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(bits.size()));
}

BENCHMARK(bench_set)->Range(64, 65'536)->MinTime(0.1);

// bench_set_clear_pair pairs set(b) with clear(b) per bit, so the headline
// is the cost of a full lifecycle of one bit at default capacity.
void bench_set_clear_pair(benchmark::State& state) {
    lob::hier_bitmap<default_capacity> bm;
    const auto bits = sample_bits(static_cast<std::size_t>(state.range(0)), default_capacity);
    for (auto _ : state) {
        for (auto b : bits)
            bm.set(b);
        for (auto b : bits)
            bm.clear(b);
        benchmark::DoNotOptimize(bm);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(bits.size()));
}

BENCHMARK(bench_set_clear_pair)->Range(64, 65'536)->MinTime(0.1);

void bench_lowest_set(benchmark::State& state) {
    lob::hier_bitmap<default_capacity> bm;
    const auto bits = sample_bits(static_cast<std::size_t>(state.range(0)), default_capacity);
    for (auto b : bits)
        bm.set(b);
    for (auto _ : state) {
        auto v = bm.lowest_set();
        benchmark::DoNotOptimize(v);
    }
}

BENCHMARK(bench_lowest_set)->Range(64, 65'536)->MinTime(0.1);

void bench_highest_set(benchmark::State& state) {
    lob::hier_bitmap<default_capacity> bm;
    const auto bits = sample_bits(static_cast<std::size_t>(state.range(0)), default_capacity);
    for (auto b : bits)
        bm.set(b);
    for (auto _ : state) {
        auto v = bm.highest_set();
        benchmark::DoNotOptimize(v);
    }
}

BENCHMARK(bench_highest_set)->Range(64, 65'536)->MinTime(0.1);

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

BENCHMARK(bench_set_clear_mixed)->Range(64, 65'536)->MinTime(0.1);

}  // namespace
