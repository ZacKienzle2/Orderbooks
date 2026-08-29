#include <lob/id_index.hpp>
#include <lob/order.hpp>
#include <lob/types.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

#include <benchmark/benchmark.h>

// The id_index is the largest single attributor of hot-path D1 misses
// (ADR-0034 measured its home-bucket probe at roughly a third of the deep
// mix's misses), yet it had no microbench of its own. These cases pin its
// probe cost at the load factor the engine actually runs, 0.5, so a layout
// or hashing change shows up here before it moves the engine benches.

namespace {

constexpr std::size_t live = 32'768;  // index capacity 2 * live, load 0.5

struct prng {
    std::uint64_t state;

    explicit prng(std::uint64_t seed) noexcept : state(seed) {}

    std::uint64_t next() noexcept {
        std::uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
};

// Values are never dereferenced; the index stores pointers into one dummy.
lob::order dummy_order{};

void fill_index(lob::id_index& idx) {
    for (lob::order_id_t id = 1; id <= live; ++id)
        idx.insert(id, &dummy_order);
}

void bench_id_index_lookup_hit(benchmark::State& state) {
    lob::id_index idx{live};
    fill_index(idx);
    prng g{0xC0FFEEULL};
    for (auto _ : state) {
        const auto id = 1 + g.next() % live;
        benchmark::DoNotOptimize(idx.lookup(id));
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(bench_id_index_lookup_hit);

void bench_id_index_lookup_miss(benchmark::State& state) {
    lob::id_index idx{live};
    fill_index(idx);
    prng g{0xC0FFEEULL};
    for (auto _ : state) {
        const auto id = live + 1 + g.next() % live;
        benchmark::DoNotOptimize(idx.lookup(id));
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(bench_id_index_lookup_miss);

// Steady-state churn: erase one live id, insert a fresh one. Exercises the
// backward-shift deletion and the insert probe at constant occupancy, the
// same pattern a cancel-heavy stream drives.
void bench_id_index_churn(benchmark::State& state) {
    lob::id_index idx{live};
    std::vector<lob::order_id_t> ids;
    ids.reserve(live);
    for (lob::order_id_t id = 1; id <= live; ++id) {
        idx.insert(id, &dummy_order);
        ids.push_back(id);
    }
    prng g{0xFEEDFACEULL};
    lob::order_id_t next_id = live + 1;
    for (auto _ : state) {
        const auto k = g.next() % live;
        idx.erase(ids[k]);
        ids[k] = next_id++;
        idx.insert(ids[k], &dummy_order);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(bench_id_index_churn);

}  // namespace
