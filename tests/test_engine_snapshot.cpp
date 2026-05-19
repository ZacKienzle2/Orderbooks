#include <lob/engine.hpp>
#include <lob/messages.hpp>
#include <lob/snapshot.hpp>
#include <lob/types.hpp>

#include "recording_publisher.hpp"

#include <cstddef>
#include <cstdint>
#include <random>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators_all.hpp>

namespace {

constexpr std::size_t ticks = 256;
constexpr std::size_t max_ord = 256;

using pub_t = lob::test::recording_publisher;
using eng_t = lob::engine<pub_t, ticks, max_ord>;

lob::submit_msg sub_with_account(lob::order_id_t id,
                                 lob::tick_t px,
                                 lob::qty_t qty,
                                 lob::side s,
                                 lob::account_id_t acct,
                                 lob::tif t = lob::tif::gtc) {
    return {.id = id, .px = px, .qty = qty, .s = s, .t = t, ._pad = 0, .account_id = acct};
}

void seed(eng_t& eng, std::uint64_t key) {
    std::mt19937_64 rng{key};
    std::uniform_int_distribution<lob::tick_t> px{0, ticks - 1};
    std::uniform_int_distribution<lob::qty_t> qty{1, 50};
    std::uniform_int_distribution<int> side_dist{0, 1};
    std::uniform_int_distribution<int> acct_dist{1, 3};
    for (lob::order_id_t id = 1; id <= 60; ++id) {
        eng.on_submit(sub_with_account(id,
                                       px(rng),
                                       qty(rng),
                                       (side_dist(rng) == 0) ? lob::side::bid : lob::side::ask,
                                       static_cast<lob::account_id_t>(acct_dist(rng))));
    }
}

void require_books_equal(const eng_t& a, const eng_t& b) {
    REQUIRE(a.book_view().bids().best() == b.book_view().bids().best());
    REQUIRE(a.book_view().asks().best() == b.book_view().asks().best());
    for (lob::tick_t px = 0; px < ticks; ++px) {
        REQUIRE(a.book_view().bids().aggregate_at(px) == b.book_view().bids().aggregate_at(px));
        REQUIRE(a.book_view().asks().aggregate_at(px) == b.book_view().asks().aggregate_at(px));
    }
}

}  // namespace

TEST_CASE("engine snapshot round-trip preserves book state", "[engine][snapshot]") {
    auto key = GENERATE(0xC0FFEEULL, 0xBADC0DEULL, 0xDEADBEEFULL);

    pub_t pub_a;
    eng_t engine_a{pub_a, lob::engine_config{}};
    seed(engine_a, key);

    lob::vector_snapshot_buffer buf;
    engine_a.snapshot(buf);
    buf.rewind();

    pub_t pub_b;
    eng_t engine_b{pub_b, lob::engine_config{}};
    REQUIRE(engine_b.restore(buf));

    require_books_equal(engine_a, engine_b);
    REQUIRE(engine_a.last_seq() == engine_b.last_seq());
}

TEST_CASE("engine snapshot continues to produce identical events after warm start",
          "[engine][snapshot]") {
    pub_t pub_a;
    eng_t engine_a{pub_a, lob::engine_config{}};
    seed(engine_a, 0xC0FFEEULL);

    lob::vector_snapshot_buffer buf;
    engine_a.snapshot(buf);
    buf.rewind();

    pub_t pub_b;
    eng_t engine_b{pub_b, lob::engine_config{}};
    REQUIRE(engine_b.restore(buf));

    // Drive an identical follow-up stream through both engines; the published
    // events should be byte-identical from this point onward.
    pub_a.clear();
    pub_b.clear();

    std::mt19937_64 rng_a{0xFEEDFACE};
    std::mt19937_64 rng_b{0xFEEDFACE};
    std::uniform_int_distribution<lob::tick_t> px{0, ticks - 1};
    std::uniform_int_distribution<lob::qty_t> qty{1, 30};
    std::uniform_int_distribution<int> side_dist{0, 1};
    for (lob::order_id_t id = 1'000; id < 1'050; ++id) {
        auto m_a = sub_with_account(id,
                                    px(rng_a),
                                    qty(rng_a),
                                    (side_dist(rng_a) == 0) ? lob::side::bid : lob::side::ask,
                                    /*acct=*/1);
        auto m_b = sub_with_account(id,
                                    px(rng_b),
                                    qty(rng_b),
                                    (side_dist(rng_b) == 0) ? lob::side::bid : lob::side::ask,
                                    /*acct=*/1);
        REQUIRE(m_a.px == m_b.px);
        engine_a.on_submit(m_a);
        engine_b.on_submit(m_b);
    }

    REQUIRE(pub_a.fills.size() == pub_b.fills.size());
    for (std::size_t i = 0; i < pub_a.fills.size(); ++i) {
        REQUIRE(pub_a.fills[i].maker == pub_b.fills[i].maker);
        REQUIRE(pub_a.fills[i].taker == pub_b.fills[i].taker);
        REQUIRE(pub_a.fills[i].px == pub_b.fills[i].px);
        REQUIRE(pub_a.fills[i].qty == pub_b.fills[i].qty);
        REQUIRE(pub_a.fills[i].seq == pub_b.fills[i].seq);
    }
    require_books_equal(engine_a, engine_b);
}

TEST_CASE("engine restore rejects header from an incompatible engine shape", "[engine][snapshot]") {
    using small_eng_t = lob::engine<pub_t, 64, 16>;

    pub_t small_pub;
    small_eng_t small_engine{small_pub, lob::engine_config{}};
    small_engine.on_submit(sub_with_account(1, 10, 5, lob::side::bid, 1));

    lob::vector_snapshot_buffer buf;
    small_engine.snapshot(buf);
    buf.rewind();

    pub_t pub_big;
    eng_t engine_big{pub_big, lob::engine_config{}};
    REQUIRE_FALSE(engine_big.restore(buf));
    // After a rejected restore the engine is left in a freshly cleared state.
    REQUIRE_FALSE(engine_big.book_view().bids().best().has_value());
    REQUIRE_FALSE(engine_big.book_view().asks().best().has_value());
}

TEST_CASE("engine restore rejects a truncated snapshot", "[engine][snapshot]") {
    pub_t pub_a;
    eng_t engine_a{pub_a, lob::engine_config{}};
    seed(engine_a, 0xC0FFEEULL);

    lob::vector_snapshot_buffer buf;
    engine_a.snapshot(buf);
    // Drop the trailing record half-way through.
    std::vector<std::byte> bytes;
    bytes.resize(buf.size() / 2);
    {
        buf.rewind();
        std::span<std::byte> first_half{bytes.data(), bytes.size()};
        REQUIRE(buf.read(first_half));
    }
    lob::vector_snapshot_buffer truncated;
    truncated.write(std::span<const std::byte>{bytes.data(), bytes.size()});
    truncated.rewind();

    pub_t pub_b;
    eng_t engine_b{pub_b, lob::engine_config{}};
    REQUIRE_FALSE(engine_b.restore(truncated));
}
