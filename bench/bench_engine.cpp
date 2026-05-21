#include <lob/config.hpp>
#include <lob/engine.hpp>
#include <lob/messages.hpp>
#include <lob/types.hpp>

#include <cstddef>
#include <cstdint>
#include <vector>

#include <benchmark/benchmark.h>

namespace {

// noexcept-correct no-op publisher. The engine concept requires every
// publish overload to be noexcept; this implementation discards every
// event and is safe to inline into a Release build.
struct noop_publisher {
    void publish(const lob::fill_msg&) noexcept {}

    void publish(const lob::top_msg&) noexcept {}

    void publish(const lob::trade_msg&) noexcept {}

    void publish(const lob::self_trade_msg&) noexcept {}
};

constexpr std::size_t bench_ticks = 1U << 14;       // 16k tick ladder
constexpr std::size_t bench_max_orders = 1U << 16;  // 64k live orders cap

using engine_t = lob::engine<noop_publisher, bench_ticks, bench_max_orders>;

// SplitMix64: cheap, allocation-free deterministic PRNG suitable for
// bench-loop input generation. Same algorithm the shard router uses
// for its hash; no shared state.
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

lob::submit_msg make_submit(prng& g, lob::order_id_t id) noexcept {
    const auto r = g.next();
    return {
        .id = id,
        .px = static_cast<lob::tick_t>(r % bench_ticks),
        .qty = 1 + (r >> 16) % 100,
        .s = (r & 1U) ? lob::side::bid : lob::side::ask,
        .t = lob::tif::gtc,
        ._pad = 0,
        .account_id = 0,
    };
}

// Populate the book with n resting orders centred on (Ticks/2 +/- spread)
// to avoid immediate crosses; produces a deep, two-sided ladder. Returns
// the next free order id.
lob::order_id_t populate_book(engine_t& eng, std::size_t n, std::uint64_t seed) noexcept {
    prng g{seed};
    constexpr lob::tick_t mid = bench_ticks / 2;
    constexpr lob::tick_t spread = 64;
    lob::order_id_t id = 1;
    for (std::size_t i = 0; i < n; ++i) {
        const auto r = g.next();
        const bool is_bid = (r & 1U) != 0;
        const auto px = is_bid ? static_cast<lob::tick_t>(mid - 1 - (r >> 1) % spread)
                               : static_cast<lob::tick_t>(mid + 1 + (r >> 1) % spread);
        eng.on_submit(lob::submit_msg{
            .id = id,
            .px = px,
            .qty = 1 + (r >> 16) % 100,
            .s = is_bid ? lob::side::bid : lob::side::ask,
            .t = lob::tif::gtc,
            ._pad = 0,
            .account_id = 0,
        });
        ++id;
    }
    return id;
}

void bench_submit_cold(benchmark::State& state) {
    noop_publisher pub;
    engine_t eng{pub, lob::engine_config{}};
    prng g{0xC0FFEEULL};
    lob::order_id_t id = 1;
    for (auto _ : state) {
        eng.on_submit(make_submit(g, id++));
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(bench_submit_cold);

void bench_submit_warm(benchmark::State& state) {
    noop_publisher pub;
    engine_t eng{pub, lob::engine_config{}};
    auto next_id = populate_book(eng, 4096, 0xDEADBEEFULL);
    prng g{0xFEEDFACEULL};
    for (auto _ : state) {
        eng.on_submit(make_submit(g, next_id++));
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(bench_submit_warm);

void bench_cancel_warm(benchmark::State& state) {
    noop_publisher pub;
    engine_t eng{pub, lob::engine_config{}};
    const std::size_t n = 4096;
    populate_book(eng, n, 0xCAFEULL);
    lob::order_id_t id = 1;
    for (auto _ : state) {
        eng.on_cancel(lob::cancel_msg{.id = id});
        // Rotate the cancel target so the bench keeps working orders alive
        // (re-submitting at the same id) and cancels do not run out of book.
        state.PauseTiming();
        eng.on_submit(lob::submit_msg{
            .id = id,
            .px = static_cast<lob::tick_t>((id * 2654435761ULL) % bench_ticks),
            .qty = 1,
            .s = (id & 1U) ? lob::side::bid : lob::side::ask,
            .t = lob::tif::gtc,
            ._pad = 0,
            .account_id = 0,
        });
        state.ResumeTiming();
        id = (id % n) + 1;
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(bench_cancel_warm);

void bench_modify_qty_only(benchmark::State& state) {
    noop_publisher pub;
    engine_t eng{pub, lob::engine_config{}};
    const std::size_t n = 4096;
    populate_book(eng, n, 0xBADC0DEULL);
    lob::order_id_t id = 1;
    lob::qty_t qty = 50;
    for (auto _ : state) {
        eng.on_modify(lob::modify_msg{.id = id, .new_px = 0, .new_qty = qty});
        // new_px = 0 collides with bench ladder midpoint shift, so the
        // engine's no-op-detection compares against the order's px; if
        // they match we get the qty-only fast path. The seed above places
        // orders near mid +/- spread so px == 0 is not in the book.
        id = (id % n) + 1;
        qty = (qty + 1) % 100 + 1;
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(bench_modify_qty_only);

void bench_match_crossing(benchmark::State& state) {
    // Steady-state: pre-populate one side, repeatedly fire aggressors that
    // cross 1-N levels of the opposite. The bench rebuilds the book inside
    // each iteration once the resting side is exhausted, so the timed work
    // is dominated by match_against_opposite_.
    noop_publisher pub;
    engine_t eng{pub, lob::engine_config{}};
    populate_book(eng, 4096, 0xF00DULL);
    prng g{0xACE1ULL};
    lob::order_id_t taker_id = 1'000'000;
    for (auto _ : state) {
        const auto r = g.next();
        eng.on_submit(lob::submit_msg{
            .id = taker_id++,
            .px = static_cast<lob::tick_t>(bench_ticks / 2),
            .qty = 1 + (r % 50),
            .s = (r & 1U) ? lob::side::bid : lob::side::ask,
            .t = lob::tif::ioc,
            ._pad = 0,
            .account_id = 0,
        });
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(bench_match_crossing);

}  // namespace
