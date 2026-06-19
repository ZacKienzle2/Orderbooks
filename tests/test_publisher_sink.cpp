#include <lob/egress_merger.hpp>
#include <lob/json_recorder.hpp>
#include <lob/messages.hpp>
#include <lob/publisher_sink.hpp>
#include <lob/shard_egress_runtime.hpp>
#include <lob/types.hpp>

#include "recording_publisher.hpp"

#include <algorithm>
#include <cstddef>
#include <sstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

namespace {

constexpr std::size_t ticks = 128;
constexpr std::size_t max_ord = 256;
constexpr std::size_t shards = 4;
constexpr std::size_t ingress = 1024;
constexpr std::size_t egress = 4096;

using runtime_t = lob::shard_egress_runtime<ticks, max_ord, shards, ingress, egress>;

lob::submit_msg sub(lob::order_id_t id, lob::tick_t px, lob::qty_t qty, lob::side s) {
    return {.id = id, .px = px, .qty = qty, .s = s, .t = lob::tif::gtc, ._pad = 0, .account_id = 0};
}

}  // namespace

TEST_CASE("publisher_sink forwards each event kind to its publisher", "[sink]") {
    lob::test::recording_publisher pub;
    lob::publisher_sink<lob::test::recording_publisher> sink{pub};

    sink.on_event(
        lob::event::make_fill(lob::fill_msg{.maker = 1, .taker = 2, .px = 10, .qty = 3, .seq = 1}),
        0);
    sink.on_event(lob::event::make_top(lob::top_msg{
                      .bid_px = 9, .ask_px = 11, .bid_qty = 2, .ask_qty = 4, .seq = 2}),
                  1);
    sink.on_event(lob::event::make_trade(lob::trade_msg{.px = 10, .qty = 1, .seq = 3}), 2);
    sink.on_event(lob::event::make_self_trade(lob::self_trade_msg{
                      .aggressor = 1, .resting = 2, .account = 5, .px = 10, .qty = 1, .seq = 4}),
                  3);

    REQUIRE(pub.fills.size() == 1);
    REQUIRE(pub.fills[0].maker == 1);
    REQUIRE(pub.tops.size() == 1);
    REQUIRE(pub.tops[0].ask_px == 11);
    REQUIRE(pub.trades.size() == 1);
    REQUIRE(pub.trades[0].px == 10);
    REQUIRE(pub.self_trades.size() == 1);
    REQUIRE(pub.self_trades[0].account == 5);
}

TEST_CASE("egress_merger streams a runtime crossing into the json recorder", "[sink]") {
    runtime_t rt{lob::engine_config{}};
    std::ostringstream out;
    lob::json_recorder rec{out};
    lob::publisher_sink<lob::json_recorder> sink{rec};
    lob::egress_merger<runtime_t, lob::publisher_sink<lob::json_recorder>> merger{
        rt, sink, lob::merger_config{.pin_thread = false}};

    rt.start();
    merger.start();

    constexpr lob::symbol_id_t sym = 123;
    while (!rt.try_submit(sym, sub(1, 100, 10, lob::side::ask))) {}
    while (!rt.try_submit(sym, sub(2, 100, 4, lob::side::bid))) {}

    rt.drain();
    rt.stop();
    merger.stop();

    const std::string s = out.str();
    REQUIRE_FALSE(s.empty());
    REQUIRE(s.back() == '\n');
    REQUIRE(s.find(R"("kind":"fill")") != std::string::npos);
    // Every forwarded event is exactly one JSON Lines record.
    REQUIRE(static_cast<std::uint64_t>(std::count(s.begin(), s.end(), '\n')) == merger.merged());
}
