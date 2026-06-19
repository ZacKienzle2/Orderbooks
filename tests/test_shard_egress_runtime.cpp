#include <lob/messages.hpp>
#include <lob/ring_publisher.hpp>
#include <lob/shard_egress_runtime.hpp>
#include <lob/shard_router.hpp>
#include <lob/spsc_ring.hpp>
#include <lob/types.hpp>

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
constexpr std::size_t ingress = 1024;
constexpr std::size_t egress = 4096;

// Reference sink for the synchronous router used as the equivalence oracle.
struct null_publisher {
    void publish(const lob::fill_msg&) noexcept {}
    void publish(const lob::top_msg&) noexcept {}
    void publish(const lob::trade_msg&) noexcept {}
    void publish(const lob::self_trade_msg&) noexcept {}
};

using runtime_t = lob::shard_egress_runtime<ticks, max_ord, shards, ingress, egress>;
using router_t = lob::shard_router<null_publisher, ticks, max_ord, shards>;

lob::submit_msg sub(lob::order_id_t id, lob::tick_t px, lob::qty_t qty, lob::side s) {
    return {.id = id, .px = px, .qty = qty, .s = s, .t = lob::tif::gtc, ._pad = 0, .account_id = 0};
}

}  // namespace

TEST_CASE("ring_publisher serialises events and reports drops", "[egress]") {
    lob::spsc_ring<lob::event, 2> ring;
    lob::ring_publisher<2> pub{ring};

    pub.publish(lob::trade_msg{.px = 10, .qty = 5, .seq = 1});
    pub.publish(lob::top_msg{.bid_px = 9, .ask_px = 11, .bid_qty = 2, .ask_qty = 3, .seq = 2});
    // Ring holds two; the third event has nowhere to go and counts as a drop.
    pub.publish(lob::fill_msg{.maker = 1, .taker = 2, .px = 10, .qty = 1, .seq = 3});
    REQUIRE(pub.dropped() == 1);

    lob::event e;
    REQUIRE(ring.try_pop(e));
    REQUIRE(e.k == lob::event::kind::trade);
    REQUIRE(ring.try_pop(e));
    REQUIRE(e.k == lob::event::kind::top);
    REQUIRE_FALSE(ring.try_pop(e));
}

TEST_CASE("shard_egress_runtime reproduces synchronous router book state", "[egress]") {
    auto seed = GENERATE(0x1234ULL, 0xCAFEULL, 0x9E3779B9ULL);
    std::mt19937_64 rng{seed};
    std::uniform_int_distribution<lob::symbol_id_t> sym_dist{1, 16};
    std::uniform_int_distribution<lob::tick_t> px{0, ticks - 1};
    std::uniform_int_distribution<lob::qty_t> qty{1, 20};
    std::uniform_int_distribution<int> op{0, 9};
    std::bernoulli_distribution is_bid{0.5};

    null_publisher ref_pub;
    runtime_t rt{lob::engine_config{}, lob::shard_runtime_config{.spin_budget = 64}};
    router_t ref{ref_pub, lob::engine_config{}};
    rt.start();

    struct entry {
        lob::symbol_id_t sym;
        lob::order_id_t id;
    };
    std::vector<entry> live;
    lob::order_id_t next_id = 1;
    lob::event sink;

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
        // Keep the egress rings drained so the producer never stalls on them.
        for (std::size_t s = 0; s < shards; ++s) {
            while (rt.try_poll(s, sink)) {
            }
        }
    }

    rt.drain();
    for (std::size_t s = 0; s < shards; ++s) {
        while (rt.try_poll(s, sink)) {
        }
    }
    rt.stop();

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

TEST_CASE("shard_egress_runtime publishes a fill on the owning shard's egress ring", "[egress]") {
    runtime_t rt{lob::engine_config{}};
    rt.start();

    constexpr lob::symbol_id_t sym = 123;
    while (!rt.try_submit(sym, sub(1, 100, 10, lob::side::ask))) {
    }
    while (!rt.try_submit(sym, sub(2, 100, 4, lob::side::bid))) {
    }
    rt.drain();

    const auto sh = rt.shard_index_for(sym);
    std::size_t fills = 0;
    std::size_t tops = 0;
    lob::event e;
    while (rt.try_poll(sh, e)) {
        if (e.k == lob::event::kind::fill) {
            ++fills;
            REQUIRE(e.body.fill.qty == 4);
        } else if (e.k == lob::event::kind::top) {
            ++tops;
        }
    }
    REQUIRE(fills == 1);
    REQUIRE(tops > 0);
    REQUIRE(rt.shard(sh).book_view().asks().aggregate_at(100) == 6);
    REQUIRE(rt.publisher(sh).dropped() == 0);

    rt.stop();
}

TEST_CASE("shard_egress_runtime drops events when an undrained egress ring fills", "[egress]") {
    lob::shard_egress_runtime<ticks, max_ord, shards, ingress, 2> rt{lob::engine_config{}};
    rt.start();

    constexpr lob::symbol_id_t sym = 7;
    const auto sh = rt.shard_index_for(sym);
    for (lob::order_id_t i = 1; i <= 60; ++i) {
        while (!rt.try_submit(sym, sub(i, static_cast<lob::tick_t>(i), 1, lob::side::bid))) {
        }
    }
    rt.drain();
    rt.stop();

    REQUIRE(rt.publisher(sh).dropped() > 0);
}
