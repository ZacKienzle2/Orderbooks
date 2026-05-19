#include <lob/messages.hpp>

#include <catch2/catch_test_macros.hpp>

#include <type_traits>

TEST_CASE("inbound command POD widths", "[messages]") {
    STATIC_REQUIRE(std::is_trivially_copyable_v<lob::submit_msg>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<lob::cancel_msg>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<lob::modify_msg>);
}

TEST_CASE("outbound event POD widths", "[messages]") {
    STATIC_REQUIRE(std::is_trivially_copyable_v<lob::fill_msg>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<lob::top_msg>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<lob::trade_msg>);
}

TEST_CASE("command tagged union dispatch", "[messages]") {
    const auto sub = lob::command::make_submit({.id = 1, .px = 100, .qty = 50, .s = lob::side::bid, .t = lob::tif::gtc});
    REQUIRE(sub.k == lob::command::kind::submit);
    REQUIRE(sub.body.submit.id == 1);
    REQUIRE(sub.body.submit.px == 100);

    const auto can = lob::command::make_cancel({.id = 99});
    REQUIRE(can.k == lob::command::kind::cancel);
    REQUIRE(can.body.cancel.id == 99);

    const auto mod = lob::command::make_modify({.id = 7, .new_px = 200, .new_qty = 10});
    REQUIRE(mod.k == lob::command::kind::modify);
    REQUIRE(mod.body.modify.new_px == 200);
    REQUIRE(mod.body.modify.new_qty == 10);
}

TEST_CASE("event tagged union dispatch", "[messages]") {
    const auto f = lob::event::make_fill({.maker = 1, .taker = 2, .px = 100, .qty = 5, .seq = 42});
    REQUIRE(f.k == lob::event::kind::fill);
    REQUIRE(f.body.fill.maker == 1);
    REQUIRE(f.body.fill.taker == 2);
    REQUIRE(f.body.fill.seq == 42);

    const auto t = lob::event::make_top({.bid_px = 99, .ask_px = 101, .bid_qty = 10, .ask_qty = 20, .seq = 43});
    REQUIRE(t.k == lob::event::kind::top);
    REQUIRE(t.body.top.bid_px == 99);
    REQUIRE(t.body.top.ask_px == 101);

    const auto tr = lob::event::make_trade({.px = 100, .qty = 5, .seq = 44});
    REQUIRE(tr.k == lob::event::kind::trade);
    REQUIRE(tr.body.trade.px == 100);
}

TEST_CASE("command and event are trivially copyable for SPSC transport", "[messages]") {
    STATIC_REQUIRE(std::is_trivially_copyable_v<lob::command>);
    STATIC_REQUIRE(std::is_trivially_copyable_v<lob::event>);
}
