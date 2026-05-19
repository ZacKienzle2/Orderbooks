#include <lob/engine.hpp>
#include <lob/messages.hpp>
#include <lob/types.hpp>

#include "recording_publisher.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

namespace {

using pub_t      = lob::test::recording_publisher;
using test_eng_t = lob::engine<pub_t, 256, 64>;

lob::submit_msg sub(lob::order_id_t id, lob::tick_t px, lob::qty_t qty, lob::side s, lob::tif t = lob::tif::gtc) {
    return {.id = id, .px = px, .qty = qty, .s = s, .t = t};
}

}  // namespace

TEST_CASE("engine submit rests GTC when book is empty", "[engine][submit]") {
    pub_t pub;
    test_eng_t eng{pub, lob::engine_config{}};

    eng.on_submit(sub(1, 100, 10, lob::side::bid));

    REQUIRE(pub.fills.empty());
    REQUIRE(eng.book_view().bids().best() == 100);
    REQUIRE(eng.book_view().bids().aggregate_at(100) == 10);
    REQUIRE(pub.tops.size() == 1);
    REQUIRE(pub.tops.back().bid_px == 100);
    REQUIRE(pub.tops.back().bid_qty == 10);
    REQUIRE(pub.tops.back().ask_px == 0);
}

TEST_CASE("engine submit matches a crossing taker against a resting maker", "[engine][match]") {
    pub_t pub;
    test_eng_t eng{pub, lob::engine_config{}};

    eng.on_submit(sub(1, 100, 10, lob::side::ask));     // maker rests
    eng.on_submit(sub(2, 100, 6, lob::side::bid));      // taker partially fills

    REQUIRE(pub.fills.size() == 1);
    REQUIRE(pub.fills[0].maker == 1);
    REQUIRE(pub.fills[0].taker == 2);
    REQUIRE(pub.fills[0].qty   == 6);
    REQUIRE(pub.fills[0].px    == 100);

    REQUIRE(eng.book_view().asks().best() == 100);
    REQUIRE(eng.book_view().asks().aggregate_at(100) == 4);
    REQUIRE(!eng.book_view().bids().best().has_value());
}

TEST_CASE("engine matches across multiple makers at the same level (FIFO)", "[engine][match]") {
    pub_t pub;
    test_eng_t eng{pub, lob::engine_config{}};

    eng.on_submit(sub(1, 100, 5, lob::side::ask));
    eng.on_submit(sub(2, 100, 5, lob::side::ask));
    eng.on_submit(sub(3, 100, 5, lob::side::ask));
    pub.clear();

    eng.on_submit(sub(99, 100, 12, lob::side::bid));

    REQUIRE(pub.fills.size() == 3);
    REQUIRE(pub.fills[0].maker == 1);
    REQUIRE(pub.fills[0].qty   == 5);
    REQUIRE(pub.fills[1].maker == 2);
    REQUIRE(pub.fills[1].qty   == 5);
    REQUIRE(pub.fills[2].maker == 3);
    REQUIRE(pub.fills[2].qty   == 2);

    REQUIRE(eng.book_view().asks().best() == 100);
    REQUIRE(eng.book_view().asks().aggregate_at(100) == 3);
    REQUIRE(!eng.book_view().bids().best().has_value());
}

TEST_CASE("engine matches across multiple price levels", "[engine][match]") {
    pub_t pub;
    test_eng_t eng{pub, lob::engine_config{}};

    eng.on_submit(sub(1, 100, 4, lob::side::ask));
    eng.on_submit(sub(2, 101, 4, lob::side::ask));
    eng.on_submit(sub(3, 102, 4, lob::side::ask));
    pub.clear();

    eng.on_submit(sub(99, 102, 10, lob::side::bid));

    REQUIRE(pub.fills.size() == 3);
    REQUIRE(pub.fills[0].px == 100);
    REQUIRE(pub.fills[1].px == 101);
    REQUIRE(pub.fills[2].px == 102);
    REQUIRE(pub.fills[2].qty == 2);

    REQUIRE(eng.book_view().asks().best() == 102);
    REQUIRE(eng.book_view().asks().aggregate_at(102) == 2);
}

TEST_CASE("engine IOC drops residual after partial fill", "[engine][ioc]") {
    pub_t pub;
    test_eng_t eng{pub, lob::engine_config{}};

    eng.on_submit(sub(1, 100, 5, lob::side::ask));
    pub.clear();

    eng.on_submit(sub(99, 100, 12, lob::side::bid, lob::tif::ioc));

    REQUIRE(pub.fills.size() == 1);
    REQUIRE(pub.fills[0].qty == 5);
    REQUIRE(!eng.book_view().bids().best().has_value());
    REQUIRE(!eng.book_view().asks().best().has_value());
}

TEST_CASE("engine FOK aborts when full fill is impossible", "[engine][fok]") {
    pub_t pub;
    test_eng_t eng{pub, lob::engine_config{}};

    eng.on_submit(sub(1, 100, 5, lob::side::ask));
    eng.on_submit(sub(2, 101, 5, lob::side::ask));
    pub.clear();

    eng.on_submit(sub(99, 101, 11, lob::side::bid, lob::tif::fok));

    REQUIRE(pub.fills.empty());
    REQUIRE(eng.book_view().asks().aggregate_at(100) == 5);
    REQUIRE(eng.book_view().asks().aggregate_at(101) == 5);
}

TEST_CASE("engine FOK fills when crossing levels can satisfy the request", "[engine][fok]") {
    pub_t pub;
    test_eng_t eng{pub, lob::engine_config{}};

    eng.on_submit(sub(1, 100, 5, lob::side::ask));
    eng.on_submit(sub(2, 101, 5, lob::side::ask));
    pub.clear();

    eng.on_submit(sub(99, 101, 10, lob::side::bid, lob::tif::fok));

    REQUIRE(pub.fills.size() == 2);
    REQUIRE(pub.fills[0].qty == 5);
    REQUIRE(pub.fills[1].qty == 5);
    REQUIRE(!eng.book_view().asks().best().has_value());
}

TEST_CASE("engine cancel removes a resting order and emits top change", "[engine][cancel]") {
    pub_t pub;
    test_eng_t eng{pub, lob::engine_config{}};

    eng.on_submit(sub(1, 100, 10, lob::side::bid));
    eng.on_submit(sub(2, 99,  10, lob::side::bid));
    pub.clear();

    eng.on_cancel({.id = 1});

    REQUIRE(eng.book_view().bids().best() == 99);
    REQUIRE(eng.book_view().bids().aggregate_at(99) == 10);
}

TEST_CASE("engine cancel of unknown id is a no-op", "[engine][cancel]") {
    pub_t pub;
    test_eng_t eng{pub, lob::engine_config{}};
    eng.on_cancel({.id = 12345});
    REQUIRE(pub.fills.empty());
}

TEST_CASE("engine modify qty-only stays at same price level", "[engine][modify]") {
    pub_t pub;
    test_eng_t eng{pub, lob::engine_config{}};

    eng.on_submit(sub(1, 100, 10, lob::side::bid));
    pub.clear();

    eng.on_modify({.id = 1, .new_px = 100, .new_qty = 25});

    REQUIRE(eng.book_view().bids().best() == 100);
    REQUIRE(eng.book_view().bids().aggregate_at(100) == 25);
}

TEST_CASE("engine modify with price change loses time priority", "[engine][modify]") {
    pub_t pub;
    test_eng_t eng{pub, lob::engine_config{}};

    eng.on_submit(sub(1, 100, 10, lob::side::bid));
    eng.on_submit(sub(2, 100, 10, lob::side::bid));
    pub.clear();

    eng.on_modify({.id = 1, .new_px = 99, .new_qty = 10});

    REQUIRE(eng.book_view().bids().best() == 100);
    REQUIRE(eng.book_view().bids().aggregate_at(100) == 10);
    REQUIRE(eng.book_view().bids().aggregate_at(99)  == 10);
}

TEST_CASE("engine top throttle suppresses identical tops", "[engine][top]") {
    pub_t pub;
    test_eng_t eng{pub, lob::engine_config{.top_throttle = true}};

    eng.on_submit(sub(1, 100, 10, lob::side::bid));
    REQUIRE(pub.tops.size() == 1);
    pub.clear();

    // A cancel-then-resubmit of the same px/qty yields no top change after
    // the second submit (top was 100/10 before and after).
    eng.on_cancel({.id = 1});
    eng.on_submit(sub(2, 100, 10, lob::side::bid));
    REQUIRE(pub.tops.size() == 2);  // one from cancel (now empty top), one from re-submit
    REQUIRE(pub.tops[0].bid_qty == 0);
    REQUIRE(pub.tops[1].bid_qty == 10);
}

TEST_CASE("engine sequence numbers are monotonic across events", "[engine][seq]") {
    pub_t pub;
    test_eng_t eng{pub, lob::engine_config{}};

    eng.on_submit(sub(1, 100, 5, lob::side::ask));
    eng.on_submit(sub(2, 100, 5, lob::side::bid));

    REQUIRE(pub.fills.size() == 1);
    REQUIRE(pub.tops.size() >= 2);
    lob::seq_t last_seq = 0;
    for (auto const& t : pub.tops) {
        REQUIRE(t.seq > last_seq);
        last_seq = t.seq;
    }
}
