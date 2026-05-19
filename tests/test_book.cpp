#include <lob/book.hpp>
#include <lob/order.hpp>
#include <lob/types.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>

namespace {

using bid_side = lob::book_side<256, lob::side::bid>;
using ask_side = lob::book_side<256, lob::side::ask>;

void prime(lob::order& o, lob::order_id_t id, lob::tick_t px, lob::qty_t qty, lob::side s) noexcept {
    o.id        = id;
    o.px        = px;
    o.remaining = qty;
    o.s         = s;
    o.t         = lob::tif::gtc;
}

}  // namespace

TEST_CASE("book_side<bid> tracks best as highest price", "[book_side]") {
    bid_side bs;
    REQUIRE(bs.empty());
    REQUIRE(!bs.best().has_value());

    lob::order a{}, b{}, c{};
    prime(a, 1, 100, 10, lob::side::bid);
    prime(b, 2, 120, 20, lob::side::bid);
    prime(c, 3, 110, 30, lob::side::bid);

    bs.add(a); REQUIRE(bs.best() == 100);
    bs.add(b); REQUIRE(bs.best() == 120);
    bs.add(c); REQUIRE(bs.best() == 120);

    REQUIRE(bs.aggregate_at(100) == 10);
    REQUIRE(bs.aggregate_at(110) == 30);
    REQUIRE(bs.aggregate_at(120) == 20);

    bs.remove(b);
    REQUIRE(bs.best() == 110);

    bs.remove(c);
    REQUIRE(bs.best() == 100);

    bs.remove(a);
    REQUIRE(bs.empty());
}

TEST_CASE("book_side<ask> tracks best as lowest price", "[book_side]") {
    ask_side bs;
    lob::order a{}, b{}, c{};
    prime(a, 1, 100, 10, lob::side::ask);
    prime(b, 2,  90, 20, lob::side::ask);
    prime(c, 3,  95, 30, lob::side::ask);

    bs.add(a); REQUIRE(bs.best() == 100);
    bs.add(b); REQUIRE(bs.best() == 90);
    bs.add(c); REQUIRE(bs.best() == 90);

    bs.remove(b);
    REQUIRE(bs.best() == 95);
    bs.remove(c);
    REQUIRE(bs.best() == 100);
    bs.remove(a);
    REQUIRE(bs.empty());
}

TEST_CASE("book_side FIFO at one level preserves time priority", "[book_side]") {
    bid_side bs;
    lob::order a{}, b{}, c{};
    prime(a, 1, 100, 10, lob::side::bid);
    prime(b, 2, 100, 20, lob::side::bid);
    prime(c, 3, 100, 30, lob::side::bid);
    bs.add(a); bs.add(b); bs.add(c);
    REQUIRE(bs.aggregate_at(100) == 60);

    auto& lvl = bs.level_at(100);
    REQUIRE(lvl.fifo.front().id == 1);
    REQUIRE(lvl.fifo.back().id  == 3);

    bs.remove(a);
    REQUIRE(lvl.fifo.front().id == 2);

    bs.remove(b);
    bs.remove(c);
    REQUIRE(bs.empty());
}

TEST_CASE("book aggregates both sides and shares arena + index", "[book]") {
    lob::book<256, 32> bk;
    REQUIRE(bk.bids().empty());
    REQUIRE(bk.asks().empty());
    REQUIRE(bk.arena().empty());
    REQUIRE(bk.index().empty());

    auto* p = bk.arena().allocate();
    REQUIRE(p != nullptr);
    prime(*p, 42, 100, 5, lob::side::bid);
    bk.bids().add(*p);
    bk.index().insert(p->id, p);

    REQUIRE(bk.bids().best() == 100);
    REQUIRE(bk.index().lookup(42) == p);

    bk.bids().remove(*p);
    bk.index().erase(p->id);
    bk.arena().deallocate(p);

    REQUIRE(bk.bids().empty());
    REQUIRE(bk.arena().empty());
    REQUIRE(bk.index().empty());
}
