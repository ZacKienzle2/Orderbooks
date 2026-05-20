#include <lob/arena.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <benchmark/benchmark.h>

namespace {

struct alignas(64) cell {
    std::uint64_t a, b, c, d;
    std::byte pad[32];
};

static_assert(sizeof(cell) == 64);

constexpr std::size_t default_capacity = 1U << 16;

void bench_alloc_only(benchmark::State& state) {
    lob::slab_arena<cell, default_capacity> arena;
    const auto n = static_cast<std::size_t>(state.range(0));
    std::vector<cell*> live;
    live.reserve(n);
    for (auto _ : state) {
        for (std::size_t i = 0; i < n; ++i)
            live.push_back(arena.allocate());
        benchmark::DoNotOptimize(live.data());
        state.PauseTiming();
        for (auto* p : live)
            arena.deallocate(p);
        live.clear();
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() *
                            static_cast<std::int64_t>(n));
}

BENCHMARK(bench_alloc_only)->Range(64, 32'768);

void bench_dealloc_only(benchmark::State& state) {
    lob::slab_arena<cell, default_capacity> arena;
    const auto n = static_cast<std::size_t>(state.range(0));
    std::vector<cell*> live;
    live.reserve(n);
    for (auto _ : state) {
        state.PauseTiming();
        for (std::size_t i = 0; i < n; ++i)
            live.push_back(arena.allocate());
        state.ResumeTiming();
        for (auto* p : live)
            arena.deallocate(p);
        live.clear();
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() *
                            static_cast<std::int64_t>(n));
}

BENCHMARK(bench_dealloc_only)->Range(64, 32'768);

void bench_alloc_dealloc_pair(benchmark::State& state) {
    lob::slab_arena<cell, default_capacity> arena;
    for (auto _ : state) {
        auto* p = arena.allocate();
        benchmark::DoNotOptimize(p);
        arena.deallocate(p);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(bench_alloc_dealloc_pair);

void bench_steady_state_churn(benchmark::State& state) {
    // Steady-state: alloc N, then alternate dealloc-oldest / alloc-newest.
    lob::slab_arena<cell, default_capacity> arena;
    const auto n = static_cast<std::size_t>(state.range(0));
    std::vector<cell*> live;
    live.reserve(n);
    for (std::size_t i = 0; i < n; ++i)
        live.push_back(arena.allocate());
    std::size_t head = 0;
    for (auto _ : state) {
        arena.deallocate(live[head]);
        live[head] = arena.allocate();
        head = (head + 1) % n;
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(bench_steady_state_churn)->Range(64, 32'768);

}  // namespace
