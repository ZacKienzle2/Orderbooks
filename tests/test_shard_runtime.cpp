#include <lob/messages.hpp>
#include <lob/shard_router.hpp>
#include <lob/shard_runtime.hpp>
#include <lob/types.hpp>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <random>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators_all.hpp>

namespace {

constexpr std::size_t ticks = 128;
constexpr std::size_t max_ord = 256;
constexpr std::size_t shards = 4;
constexpr std::size_t ring = 1024;

// Discards every event. The functional tests assert on resting book state,
// which the engine owns; event delivery is exercised separately.
struct null_publisher {
    void publish(const lob::fill_msg&) noexcept {}
    void publish(const lob::top_msg&) noexcept {}
    void publish(const lob::trade_msg&) noexcept {}
    void publish(const lob::self_trade_msg&) noexcept {}
};

// Thread-safe tally. Worker threads call publish concurrently, so the
// counters must tolerate simultaneous increments from every shard.
struct counting_publisher {
    std::atomic<std::size_t> fills{0};
    std::atomic<std::size_t> tops{0};
    std::atomic<std::size_t> trades{0};
    std::atomic<std::size_t> self_trades{0};

    void publish(const lob::fill_msg&) noexcept { fills.fetch_add(1, std::memory_order_relaxed); }
    void publish(const lob::top_msg&) noexcept { tops.fetch_add(1, std::memory_order_relaxed); }
    void publish(const lob::trade_msg&) noexcept { trades.fetch_add(1, std::memory_order_relaxed); }
    void publish(const lob::self_trade_msg&) noexcept {
        self_trades.fetch_add(1, std::memory_order_relaxed);
    }
};

using runtime_t = lob::shard_runtime<null_publisher, ticks, max_ord, shards, ring>;
using router_t = lob::shard_router<null_publisher, ticks, max_ord, shards>;

lob::submit_msg sub(lob::order_id_t id, lob::tick_t px, lob::qty_t qty, lob::side s) {
    return {.id = id, .px = px, .qty = qty, .s = s, .t = lob::tif::gtc, ._pad = 0, .account_id = 0};
}

}  // namespace

TEST_CASE("shard_runtime reproduces synchronous router book state", "[runtime]") {
    auto seed = GENERATE(0xA11CE5ULL, 0xB0BCAFEULL, 0xF00DBA5EULL);
    std::mt19937_64 rng{seed};
    std::uniform_int_distribution<lob::symbol_id_t> sym_dist{1, 16};
    std::uniform_int_distribution<lob::tick_t> px{0, ticks - 1};
    std::uniform_int_distribution<lob::qty_t> qty{1, 20};
    std::uniform_int_distribution<int> op{0, 9};
    std::bernoulli_distribution is_bid{0.5};

    null_publisher rt_pub;
    null_publisher ref_pub;
    runtime_t rt{rt_pub, lob::engine_config{}, lob::shard_runtime_config{.spin_budget = 64}};
    router_t ref{ref_pub, lob::engine_config{}};
    rt.start();

    struct entry {
        lob::symbol_id_t sym;
        lob::order_id_t id;
    };
    std::vector<entry> live;
    lob::order_id_t next_id = 1;

    for (std::size_t step = 0; step < 4'000; ++step) {
        const int roll = op(rng);
        if (roll < 6 || live.empty()) {
            const auto sym = sym_dist(rng);
            const auto id = next_id++;
            const auto side = is_bid(rng) ? lob::side::bid : lob::side::ask;
            const auto m = sub(id, px(rng), qty(rng), side);
            while (!rt.try_submit(sym, m)) {
            }
            ref.on_submit(sym, m);
            live.push_back({sym, id});
        } else if (roll < 8) {
            std::uniform_int_distribution<std::size_t> pick{0, live.size() - 1};
            const auto e = live[pick(rng)];
            const lob::modify_msg m{.id = e.id, .new_px = px(rng), .new_qty = qty(rng)};
            while (!rt.try_modify(e.sym, m)) {
            }
            ref.on_modify(e.sym, m);
        } else {
            std::uniform_int_distribution<std::size_t> pick{0, live.size() - 1};
            const auto idx = pick(rng);
            const auto e = live[idx];
            const lob::cancel_msg m{.id = e.id};
            while (!rt.try_cancel(e.sym, m)) {
            }
            ref.on_cancel(e.sym, m);
            live[idx] = live.back();
            live.pop_back();
        }
    }

    rt.drain();
    rt.stop();

    // Per-shard engines are independent, so identical per-shard arrival order
    // must yield byte-identical ladders regardless of cross-shard timing.
    for (std::size_t s = 0; s < shards; ++s) {
        const auto& rb = rt.shard(s).book_view();
        const auto& fb = ref.shard(s).book_view();
        REQUIRE(rb.bids().best() == fb.bids().best());
        REQUIRE(rb.asks().best() == fb.asks().best());
        for (lob::tick_t t = 0; t < ticks; ++t) {
            REQUIRE(rb.bids().aggregate_at(t) == fb.bids().aggregate_at(t));
            REQUIRE(rb.asks().aggregate_at(t) == fb.asks().aggregate_at(t));
        }
    }
}

TEST_CASE("shard_runtime matches a crossing pair and drains to empty", "[runtime]") {
    counting_publisher pub;
    lob::shard_runtime<counting_publisher, ticks, max_ord, shards, ring> rt{pub,
                                                                            lob::engine_config{}};
    rt.start();

    constexpr lob::symbol_id_t sym = 123;
    while (!rt.try_submit(sym, sub(1, 100, 10, lob::side::ask))) {
    }
    while (!rt.try_submit(sym, sub(2, 100, 4, lob::side::bid))) {
    }
    rt.drain();

    REQUIRE(pub.fills.load() == 1);

    const auto& book = rt.shard(rt.shard_index_for(sym)).book_view();
    REQUIRE_FALSE(book.bids().best().has_value());
    REQUIRE(book.asks().best() == 100);
    REQUIRE(book.asks().aggregate_at(100) == 6);

    rt.stop();
}

TEST_CASE("shard_runtime try_submit reports backpressure when the ring is full", "[runtime]") {
    null_publisher pub;
    // Capacity two, workers never started, so nothing drains the ring.
    lob::shard_runtime<null_publisher, ticks, max_ord, 1, 2> rt{pub, lob::engine_config{}};

    constexpr lob::symbol_id_t sym = 5;
    REQUIRE(rt.try_submit(sym, sub(1, 10, 1, lob::side::bid)));
    REQUIRE(rt.try_submit(sym, sub(2, 11, 1, lob::side::bid)));
    REQUIRE_FALSE(rt.try_submit(sym, sub(3, 12, 1, lob::side::bid)));
}

TEST_CASE("shard_runtime survives repeated start and stop cycles", "[runtime]") {
    counting_publisher pub;
    lob::shard_runtime<counting_publisher, ticks, max_ord, shards, ring> rt{pub,
                                                                            lob::engine_config{}};

    constexpr lob::symbol_id_t sym = 9;
    lob::order_id_t id = 1;
    for (int cycle = 0; cycle < 3; ++cycle) {
        rt.start();
        while (!rt.try_submit(sym, sub(id, 40, 2, lob::side::bid))) {
        }
        while (!rt.try_cancel(sym, lob::cancel_msg{.id = id})) {
        }
        ++id;
        rt.drain();
        rt.stop();
    }

    const auto& book = rt.shard(rt.shard_index_for(sym)).book_view();
    REQUIRE_FALSE(book.bids().best().has_value());
}
