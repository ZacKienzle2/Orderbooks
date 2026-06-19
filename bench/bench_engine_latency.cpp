#include <lob/engine.hpp>
#include <lob/latency_histogram.hpp>
#include <lob/messages.hpp>
#include <lob/types.hpp>

#include <cstddef>
#include <cstdint>
#include <random>

#include <benchmark/benchmark.h>

#if !defined(__x86_64__) && !defined(__i386__)
    #include <chrono>
#endif

namespace {

// Discards events so the measurement isolates the matching path, not a sink.
struct null_publisher {
    void publish(const lob::fill_msg&) noexcept {}

    void publish(const lob::top_msg&) noexcept {}

    void publish(const lob::trade_msg&) noexcept {}

    void publish(const lob::self_trade_msg&) noexcept {}
};

constexpr std::size_t ticks = 4096;
constexpr std::size_t max_orders = std::size_t{1} << 16;

// A coarse, low-overhead timestamp. On x86 it is the time-stamp counter, read
// with a handful of cycles of overhead so it does not swamp a sub-microsecond
// operation; the unit is reference cycles. Elsewhere it is the monotonic clock
// in nanoseconds.
[[nodiscard]] std::uint64_t timestamp() noexcept {
#if defined(__x86_64__) || defined(__i386__)
    return __builtin_ia32_rdtsc();
#else
    return static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
#endif
}

lob::submit_msg make_bid(lob::order_id_t id, lob::tick_t px, lob::qty_t qty) {
    return {.id = id,
            .px = px,
            .qty = qty,
            .s = lob::side::bid,
            .t = lob::tif::gtc,
            ._pad = 0,
            .account_id = 0};
}

// Per-operation latency distribution of engine::on_submit on the resting path.
// Each iteration times one submit of a non-crossing bid into a warmed book,
// records the sample into an HDR histogram, then cancels the order so the book
// and the arena stay bounded. The histogram percentiles are reported as
// benchmark counters alongside the throughput figure.
void bench_submit_latency(benchmark::State& state) {
    null_publisher pub;
    lob::engine<null_publisher, ticks, max_orders> eng{pub, lob::engine_config{}};
    lob::latency_histogram hist{1'000'000, 3};

    std::mt19937_64 rng{0xA11CE5ULL};
    std::uniform_int_distribution<lob::tick_t> px{1, ticks - 2};
    std::uniform_int_distribution<lob::qty_t> qty{1, 100};

    lob::order_id_t next_id = 1;
    for (std::size_t i = 0; i < 2000; ++i) {
        eng.on_submit(make_bid(next_id++, px(rng), qty(rng)));
    }

    for (auto _ : state) {
        const auto m = make_bid(next_id++, px(rng), qty(rng));
        const auto start = timestamp();
        eng.on_submit(m);
        const auto stop = timestamp();
        hist.record(stop - start);
        benchmark::ClobberMemory();
        eng.on_cancel(lob::cancel_msg{.id = m.id});
    }

    state.SetItemsProcessed(state.iterations());
    state.counters["p50"] = static_cast<double>(hist.value_at_percentile(50.0));
    state.counters["p99"] = static_cast<double>(hist.value_at_percentile(99.0));
    state.counters["p99.9"] = static_cast<double>(hist.value_at_percentile(99.9));
    state.counters["max"] = static_cast<double>(hist.max());
}

BENCHMARK(bench_submit_latency);

}  // namespace
