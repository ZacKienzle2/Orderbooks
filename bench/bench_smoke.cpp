#include <lob/types.hpp>

#include <benchmark/benchmark.h>

namespace {

void bench_opposite(benchmark::State& state) {
    auto s = lob::side::bid;
    for (auto _ : state) {
        s = lob::opposite(s);
        benchmark::DoNotOptimize(s);
    }
}

BENCHMARK(bench_opposite);

}  // namespace
