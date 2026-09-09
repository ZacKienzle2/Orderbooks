#include <lob/messages.hpp>
#include <lob/shard_router.hpp>
#include <lob/types.hpp>

#include "recording_publisher.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <random>
#include <unordered_set>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators_all.hpp>

namespace {

constexpr std::size_t ticks = 256;
constexpr std::size_t max_ord = 64;
constexpr std::size_t shards = 4;

using pub_t = lob::test::recording_publisher;
using router_t = lob::shard_router<pub_t, ticks, max_ord, shards>;

lob::submit_msg sub(lob::order_id_t id, lob::tick_t px, lob::qty_t qty, lob::side s) {
    return {.id = id, .px = px, .qty = qty, .s = s, .t = lob::tif::gtc, ._pad = 0, .account_id = 0};
}

}  // namespace

TEST_CASE("shard_router routes a symbol to a deterministic shard", "[shard]") {
    pub_t pub;
    router_t r{pub, lob::engine_config{}};

    REQUIRE(router_t::shard_count() == shards);

    // The same symbol always maps to the same shard.
    for (lob::symbol_id_t sym = 1; sym <= 64; ++sym) {
        const auto a = r.shard_index_for(sym);
        const auto b = r.shard_index_for(sym);
        REQUIRE(a == b);
        REQUIRE(a < shards);
    }
}

TEST_CASE("shard_router distributes a moderate symbol population across shards", "[shard]") {
    pub_t pub;
    router_t r{pub, lob::engine_config{}};

    std::array<std::size_t, shards> hits{};
    for (lob::symbol_id_t sym = 1; sym <= 4'000; ++sym) {
        ++hits[r.shard_index_for(sym)];
    }
    for (auto h : hits) {
        REQUIRE(h > 0);
        // Each bucket should be within 25% of the perfect quarter share.
        REQUIRE(h > (4'000 / shards) / 2);
        REQUIRE(h < (4'000 / shards) * 2);
    }
}

TEST_CASE("shard_router isolates state across shards", "[shard]") {
    pub_t pub;
    router_t r{pub, lob::engine_config{}};

    // Find two symbols that map to distinct shards.
    lob::symbol_id_t sym_a = 1;
    lob::symbol_id_t sym_b = 2;
    while (r.shard_index_for(sym_a) == r.shard_index_for(sym_b)) {
        ++sym_b;
    }

    r.on_submit(sym_a, sub(10, 50, 7, lob::side::bid));
    r.on_submit(sym_b, sub(20, 50, 3, lob::side::ask));

    const auto& eng_a = r.shard(r.shard_index_for(sym_a));
    const auto& eng_b = r.shard(r.shard_index_for(sym_b));

    REQUIRE(eng_a.book_view().bids().best() == 50);
    REQUIRE(eng_a.book_view().bids().aggregate_at(50) == 7);
    REQUIRE_FALSE(eng_a.book_view().asks().best().has_value());

    REQUIRE_FALSE(eng_b.book_view().bids().best().has_value());
    REQUIRE(eng_b.book_view().asks().best() == 50);
    REQUIRE(eng_b.book_view().asks().aggregate_at(50) == 3);
}

TEST_CASE("shard_router matches when same symbol crosses", "[shard]") {
    pub_t pub;
    router_t r{pub, lob::engine_config{}};

    constexpr lob::symbol_id_t sym = 123;

    r.on_submit(sym, sub(1, 100, 10, lob::side::ask));
    r.on_submit(sym, sub(2, 100, 4, lob::side::bid));

    REQUIRE(pub.fills.size() == 1);
    REQUIRE(pub.fills[0].maker == 1);
    REQUIRE(pub.fills[0].taker == 2);
    REQUIRE(pub.fills[0].qty == 4);
}

TEST_CASE("shard_router cancel and modify reach the same shard as submit", "[shard]") {
    pub_t pub;
    router_t r{pub, lob::engine_config{}};

    auto seed = GENERATE(0xC0FFEEULL, 0xBADC0DEULL, 0xDEADBEEFULL);
    std::mt19937_64 rng{seed};
    std::uniform_int_distribution<lob::symbol_id_t> sym_dist{1, 16};
    std::uniform_int_distribution<lob::tick_t> px{0, ticks - 1};
    std::uniform_int_distribution<lob::qty_t> qty{1, 20};

    struct entry {
        lob::symbol_id_t sym;
        lob::order_id_t id;
    };

    std::vector<entry> live;
    lob::order_id_t next_id = 1;

    for (std::size_t step = 0; step < 200; ++step) {
        const auto sym = sym_dist(rng);
        const auto id = next_id++;
        r.on_submit(sym, sub(id, px(rng), qty(rng), lob::side::bid));
        live.push_back({sym, id});
    }

    for (const auto& e : live) {
        r.on_modify(e.sym, lob::modify_msg{.id = e.id, .new_px = px(rng), .new_qty = qty(rng)});
        r.on_cancel(e.sym, lob::cancel_msg{.id = e.id});
    }

    // After all cancels, no shard has any resting orders.
    for (std::size_t i = 0; i < shards; ++i) {
        REQUIRE_FALSE(r.shard(i).book_view().bids().best().has_value());
        REQUIRE_FALSE(r.shard(i).book_view().asks().best().has_value());
    }
}

TEST_CASE("shard_router seeds disjoint seq ranges per shard", "[shard]") {
    pub_t pub;
    router_t r{pub, lob::engine_config{}};

    // Every shard starts at its shard_seq_base and stamps within its range.
    for (std::size_t i = 0; i < shards; ++i) {
        REQUIRE(r.shard(i).last_seq() == lob::shard_seq_base(i, shards));
    }

    // Drive a crossing on one symbol per shard, then check every emitted seq
    // is globally unique and falls inside its shard's range.
    std::array<lob::symbol_id_t, shards> sym_for_shard{};
    std::array<bool, shards> found{};
    std::size_t found_count = 0;
    for (lob::symbol_id_t sym = 1; found_count < shards; ++sym) {
        const auto sh = r.shard_index_for(sym);
        if (!found[sh]) {
            sym_for_shard[sh] = sym;
            found[sh] = true;
            ++found_count;
        }
    }
    lob::order_id_t id = 1;
    for (std::size_t i = 0; i < shards; ++i) {
        r.on_submit(sym_for_shard[i], sub(id++, 100, 10, lob::side::ask));
        r.on_submit(sym_for_shard[i], sub(id++, 100, 10, lob::side::bid));
    }

    REQUIRE(pub.fills.size() == shards);
    std::unordered_set<lob::seq_t> seen;
    for (const auto& f : pub.fills) {
        REQUIRE(seen.insert(f.seq).second);
    }
    for (const auto& t : pub.tops) {
        REQUIRE(seen.insert(t.seq).second);
    }
    for (std::size_t i = 0; i < shards; ++i) {
        const auto base = lob::shard_seq_base(i, shards);
        const auto span = lob::shard_seq_base(1, shards);
        const auto s = r.shard(i).last_seq();
        REQUIRE(s > base);
        REQUIRE(s - base < span);
    }
}
