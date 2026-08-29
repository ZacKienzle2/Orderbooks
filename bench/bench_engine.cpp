#include <lob/config.hpp>
#include <lob/engine.hpp>
#include <lob/messages.hpp>
#include <lob/types.hpp>

#include <chrono>
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

    void publish(const lob::reject_msg&) noexcept {}
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
        .s = (r & 1U) != 0 ? lob::side::bid : lob::side::ask,
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
    // Manual timing over a batch of cancels, with the refill outside the
    // measured window. The previous shape paused and resumed the timer
    // around a refill inside every iteration, and PauseTiming costs on the
    // order of a hundred nanoseconds per call, so the reported "cancel" was
    // mostly timer tax rather than the ~tens-of-nanoseconds cancel itself.
    noop_publisher pub;
    engine_t eng{pub, lob::engine_config{}};
    constexpr std::size_t n = 4096;
    constexpr std::size_t batch = 1024;
    populate_book(eng, n, 0xCAFEULL);
    lob::order_id_t id = 1;
    for (auto _ : state) {
        const auto first = id;
        const auto t0 = std::chrono::steady_clock::now();
        for (std::size_t i = 0; i < batch; ++i) {
            eng.on_cancel(lob::cancel_msg{.id = id});
            id = (id % n) + 1;
        }
        const auto t1 = std::chrono::steady_clock::now();
        state.SetIterationTime(std::chrono::duration<double>(t1 - t0).count());
        // Untimed refill of exactly the ids just cancelled, deterministic px
        // per id so the book shape stays stationary across iterations.
        auto refill_id = first;
        for (std::size_t i = 0; i < batch; ++i) {
            eng.on_submit(lob::submit_msg{
                .id = refill_id,
                .px = static_cast<lob::tick_t>((refill_id * 2654435761ULL) % bench_ticks),
                .qty = 1,
                .s = (refill_id & 1U) != 0 ? lob::side::bid : lob::side::ask,
                .t = lob::tif::gtc,
                ._pad = 0,
                .account_id = 0,
            });
            refill_id = (refill_id % n) + 1;
        }
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(batch));
}

BENCHMARK(bench_cancel_warm)->UseManualTime();

void bench_modify_qty_only(benchmark::State& state) {
    // Quantity-only modify on a resting order, the in-place aggregate
    // mutation. The ladder is deterministic so every modify carries the
    // order's own resting px (the qty-only path requires new_px == px), and
    // the quantity alternates so the genuine-no-op early return never
    // fires. The previous shape sent new_px = 0, which repriced the whole
    // book to tick zero, matched it away against itself, and then measured
    // failed id lookups on dead orders.
    noop_publisher pub;
    engine_t eng{pub, lob::engine_config{}};
    constexpr std::size_t n = 4096;
    constexpr lob::tick_t base = bench_ticks / 2;
    const auto px_of = [](lob::order_id_t oid) noexcept {
        return static_cast<lob::tick_t>(base + 1 + (oid % 512));
    };
    for (lob::order_id_t oid = 1; oid <= n; ++oid) {
        eng.on_submit(lob::submit_msg{
            .id = oid,
            .px = px_of(oid),
            .qty = 5,
            .s = lob::side::ask,
            .t = lob::tif::gtc,
            ._pad = 0,
            .account_id = 0,
        });
    }
    lob::order_id_t id = 1;
    lob::qty_t flip = 0;
    for (auto _ : state) {
        eng.on_modify(lob::modify_msg{.id = id, .new_px = px_of(id), .new_qty = 5 + (flip & 1U)});
        id = (id % n) + 1;
        if (id == 1)
            ++flip;
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(bench_modify_qty_only);

void bench_modify_price(benchmark::State& state) {
    // Non-crossing price-move modify, the in-place relink fast path. The book
    // is one-sided (asks only, no bids), so every move stays resting and never
    // falls back to cancel-and-resubmit; ids and target prices range across a
    // wide band so each modify touches a cold order record, level, and bitmap.
    noop_publisher pub;
    engine_t eng{pub, lob::engine_config{}};
    constexpr std::size_t n = 4096;
    constexpr lob::tick_t lo = bench_ticks / 2;
    constexpr lob::tick_t hi = lo + 2048;
    prng g{0x5EEDF00DULL};
    for (lob::order_id_t id = 1; id <= n; ++id) {
        eng.on_submit(lob::submit_msg{
            .id = id,
            .px = static_cast<lob::tick_t>(lo + g.next() % (hi - lo)),
            .qty = 1 + g.next() % 50,
            .s = lob::side::ask,
            .t = lob::tif::gtc,
            ._pad = 0,
            .account_id = 0,
        });
    }
    lob::order_id_t id = 1;
    for (auto _ : state) {
        eng.on_modify(lob::modify_msg{
            .id = id,
            .new_px = static_cast<lob::tick_t>(lo + g.next() % (hi - lo)),
            .new_qty = 1 + g.next() % 50,
        });
        id = id % n + 1;
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(bench_modify_price);

void bench_match_crossing(benchmark::State& state) {
    // Single-maker cross: an untimed maker rests at mid, then the timed IOC
    // taker consumes it exactly, so every measured iteration walks the match
    // loop, publishes a fill and a trade, empties the level, and clears the
    // bitmap bit. The previous shape fired takers at a fixed mid price with
    // no refill, so once the initial book receded past mid it measured the
    // no-fill IOC path rather than a match.
    noop_publisher pub;
    engine_t eng{pub, lob::engine_config{}};
    prng g{0xACE1ULL};
    constexpr lob::tick_t mid = bench_ticks / 2;
    constexpr std::size_t batch = 256;
    lob::order_id_t maker_id = 1;
    lob::order_id_t taker_id = 1'000'000'000;
    std::vector<lob::qty_t> qtys(batch);
    for (auto _ : state) {
        for (std::size_t i = 0; i < batch; ++i) {
            qtys[i] = 1 + (g.next() % 50);
            eng.on_submit(lob::submit_msg{
                .id = maker_id++,
                .px = mid,
                .qty = qtys[i],
                .s = lob::side::ask,
                .t = lob::tif::gtc,
                ._pad = 0,
                .account_id = 0,
            });
        }
        const auto t0 = std::chrono::steady_clock::now();
        for (std::size_t i = 0; i < batch; ++i) {
            eng.on_submit(lob::submit_msg{
                .id = taker_id++,
                .px = mid,
                .qty = qtys[i],
                .s = lob::side::bid,
                .t = lob::tif::ioc,
                ._pad = 0,
                .account_id = 0,
            });
        }
        const auto t1 = std::chrono::steady_clock::now();
        state.SetIterationTime(std::chrono::duration<double>(t1 - t0).count());
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(batch));
}

BENCHMARK(bench_match_crossing)->UseManualTime();

void bench_match_deep_sweep(benchmark::State& state) {
    // Deep single-level sweep. Rest a tall FIFO at one price, then fire one
    // aggressor that consumes the whole stack. Each fill advances to the next
    // resting order through the intrusive list, so the timed work is dominated
    // by FIFO pointer-chasing across arena slots. This is the path the match
    // loop's software prefetch targets; the resting stack is rebuilt outside
    // the timed region so the measurement isolates the sweep.
    noop_publisher pub;
    engine_t eng{pub, lob::engine_config{}};
    constexpr lob::tick_t px = bench_ticks / 2;
    constexpr std::size_t depth = 512;
    lob::order_id_t maker_id = 1;
    lob::order_id_t taker_id = 1'000'000'000;

    const auto refill = [&] {
        for (std::size_t i = 0; i < depth; ++i) {
            eng.on_submit(lob::submit_msg{
                .id = maker_id++,
                .px = px,
                .qty = 1,
                .s = lob::side::ask,
                .t = lob::tif::gtc,
                ._pad = 0,
                .account_id = 0,
            });
        }
    };
    refill();
    for (auto _ : state) {
        // Manual timing keeps the untimed refill out of the measurement
        // without the per-iteration PauseTiming tax, which is itself on the
        // order of a hundred nanoseconds and previously inflated the sweep.
        const auto t0 = std::chrono::steady_clock::now();
        eng.on_submit(lob::submit_msg{
            .id = taker_id++,
            .px = px,
            .qty = static_cast<lob::qty_t>(depth),
            .s = lob::side::bid,
            .t = lob::tif::ioc,
            ._pad = 0,
            .account_id = 0,
        });
        const auto t1 = std::chrono::steady_clock::now();
        state.SetIterationTime(std::chrono::duration<double>(t1 - t0).count());
        refill();
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(depth));
}

BENCHMARK(bench_match_deep_sweep)->UseManualTime();

// FOK precheck against a level holding the aggressor's own resting orders
// under a cancelling policy, the path that walks the level FIFO instead of
// reading the O(1) aggregate. The request exceeds the fillable quantity, so
// the precheck rejects, nothing mutates, and the loop measures the walk
// itself over a 512-deep level with alternating ownership.
void bench_fok_precheck_self_cross(benchmark::State& state) {
    noop_publisher pub;
    engine_t eng{pub, lob::engine_config{.self_cross = lob::self_cross_policy::cancel_oldest}};
    constexpr lob::tick_t px = bench_ticks / 2;
    constexpr std::size_t depth = 512;
    for (std::size_t i = 0; i < depth; ++i) {
        eng.on_submit(lob::submit_msg{
            .id = static_cast<lob::order_id_t>(1 + i),
            .px = px,
            .qty = 1,
            .s = lob::side::ask,
            .t = lob::tif::gtc,
            ._pad = 0,
            .account_id = static_cast<lob::account_id_t>(1 + (i & 1U)),
        });
    }
    lob::order_id_t taker_id = 1'000'000'000;
    for (auto _ : state) {
        eng.on_submit(lob::submit_msg{
            .id = taker_id++,
            .px = px,
            .qty = depth,  // half the level is own liquidity, so this is short
            .s = lob::side::bid,
            .t = lob::tif::fok,
            ._pad = 0,
            .account_id = 1,
        });
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(bench_fok_precheck_self_cross);

// Control: the same shaped request with no account takes the aggregate path,
// one O(1) read per crossing level.
void bench_fok_precheck_aggregate(benchmark::State& state) {
    noop_publisher pub;
    engine_t eng{pub, lob::engine_config{}};
    constexpr lob::tick_t px = bench_ticks / 2;
    constexpr std::size_t depth = 512;
    for (std::size_t i = 0; i < depth; ++i) {
        eng.on_submit(lob::submit_msg{
            .id = static_cast<lob::order_id_t>(1 + i),
            .px = px,
            .qty = 1,
            .s = lob::side::ask,
            .t = lob::tif::gtc,
            ._pad = 0,
            .account_id = 0,
        });
    }
    lob::order_id_t taker_id = 1'000'000'000;
    for (auto _ : state) {
        eng.on_submit(lob::submit_msg{
            .id = taker_id++,
            .px = px,
            .qty = depth + 1,  // one past the aggregate, so the precheck rejects
            .s = lob::side::bid,
            .t = lob::tif::fok,
            ._pad = 0,
            .account_id = 0,
        });
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations());
}

BENCHMARK(bench_fok_precheck_aggregate);

}  // namespace
