#include <lob/config.hpp>
#include <lob/engine.hpp>
#include <lob/types.hpp>

#include "../apps/gateway/wire.hpp"

#include <cstdint>
#include <memory>

#include <catch2/catch_test_macros.hpp>

namespace {

constexpr std::size_t ticks = 64;
constexpr std::size_t max_orders = 128;

using engine_t = lob::engine<lob_gateway::accum_pub, ticks, max_orders>;

struct fixture {
    lob_gateway::accum_pub pub;
    std::unique_ptr<engine_t> eng = std::make_unique<engine_t>(pub, lob::engine_config{});

    lob_gateway::wire_ack apply(const lob_gateway::wire_order& wo) {
        lob_gateway::wire_ack ack{};
        lob_gateway::apply_order(*eng, pub, wo, ack);
        return ack;
    }
};

[[nodiscard]] lob_gateway::wire_order
submit(std::uint64_t id, std::uint32_t px, std::uint64_t qty, std::uint8_t tif = 0) {
    return lob_gateway::wire_order{
        .id = id, .qty = qty, .px = px, .new_px = 0, .op = 0, .side = 0, .tif = tif, .pad = 0};
}

}  // namespace

TEST_CASE("gateway rejects a submit with px at or beyond the tick ladder", "[gateway]") {
    fixture f;
    const auto seq_before = f.eng->last_seq();

    for (const std::uint32_t px : {static_cast<std::uint32_t>(ticks),
                                   static_cast<std::uint32_t>(ticks + 1),
                                   std::uint32_t{0xffffffff}}) {
        const auto ack = f.apply(submit(1, px, 5));
        CHECK(ack.id == 1);
        CHECK(ack.status == lob_gateway::ack_rejected);
        CHECK(ack.filled == 0);
    }

    // The book is unmodified: no side has a best, no event consumed a seq,
    // and the rejected id is not resting (a cancel of it is a no-op).
    CHECK(!f.eng->book_view().bids().best().has_value());
    CHECK(!f.eng->book_view().asks().best().has_value());
    CHECK(f.eng->last_seq() == seq_before);
}

TEST_CASE("gateway rejects a modify with new_px at or beyond the tick ladder", "[gateway]") {
    fixture f;
    REQUIRE(f.apply(submit(7, 10, 5)).status == lob_gateway::ack_accepted);
    const auto seq_before = f.eng->last_seq();

    const lob_gateway::wire_order wo{
        .id = 7, .qty = 5, .px = 0, .new_px = ticks, .op = 2, .side = 0, .tif = 0, .pad = 0};
    const auto ack = f.apply(wo);
    CHECK(ack.status == lob_gateway::ack_rejected);

    // The resting order is untouched at its original price.
    REQUIRE(f.eng->book_view().bids().best().has_value());
    CHECK(*f.eng->book_view().bids().best() == 10);
    CHECK(f.eng->book_view().bids().aggregate_at(10) == 5);
    CHECK(f.eng->last_seq() == seq_before);
}

TEST_CASE("gateway rejects a submit with zero qty or an unknown tif", "[gateway]") {
    fixture f;

    CHECK(f.apply(submit(1, 10, 0)).status == lob_gateway::ack_rejected);
    CHECK(f.apply(submit(2, 10, 5, 3)).status == lob_gateway::ack_rejected);
    CHECK(f.apply(submit(3, 10, 5, 0xff)).status == lob_gateway::ack_rejected);

    CHECK(!f.eng->book_view().bids().best().has_value());
    CHECK(!f.eng->book_view().asks().best().has_value());
}

TEST_CASE("gateway accepts and dispatches a valid order stream", "[gateway]") {
    fixture f;

    // Resting ask, then a crossing bid that fills against it.
    const lob_gateway::wire_order ask{
        .id = 1, .qty = 5, .px = 10, .new_px = 0, .op = 0, .side = 1, .tif = 0, .pad = 0};
    CHECK(f.apply(ask).status == lob_gateway::ack_accepted);

    const auto ack = f.apply(submit(2, 10, 5, 1));
    CHECK(ack.status == lob_gateway::ack_filled);
    CHECK(ack.filled == 5);
    CHECK(ack.last_px == 10);

    // Cancel of an unknown id still acks as processed.
    const lob_gateway::wire_order cxl{
        .id = 99, .qty = 0, .px = 0, .new_px = 0, .op = 1, .side = 0, .tif = 0, .pad = 0};
    CHECK(f.apply(cxl).status == lob_gateway::ack_processed);
}

TEST_CASE("gateway rejects quantities above the configured cap", "[gateway]") {
    fixture f;
    const auto cap = f.eng->config().max_order_qty;

    CHECK(f.apply(submit(1, 10, cap + 1)).status == lob_gateway::ack_rejected);
    CHECK(f.apply(submit(2, 10, ~std::uint64_t{0})).status == lob_gateway::ack_rejected);
    CHECK(!f.eng->book_view().bids().best().has_value());

    // A modify above the cap is rejected and the resting order is untouched.
    REQUIRE(f.apply(submit(3, 10, cap)).status == lob_gateway::ack_accepted);
    const lob_gateway::wire_order big_modify{
        .id = 3, .qty = cap + 1, .px = 0, .new_px = 12, .op = 2, .side = 0, .tif = 0, .pad = 0};
    CHECK(f.apply(big_modify).status == lob_gateway::ack_rejected);
    CHECK(f.eng->book_view().bids().aggregate_at(10) == cap);
}

TEST_CASE("gateway acks rejected when the arena refuses to rest an order", "[gateway]") {
    fixture f;

    // Fill the arena to MaxOrders resting bids, then the next submit that
    // must rest cannot, and the ack must say so rather than "accepted".
    for (std::uint64_t id = 1; id <= max_orders; ++id) {
        REQUIRE(f.apply(submit(id, 10, 1)).status == lob_gateway::ack_accepted);
    }
    const auto ack = f.apply(submit(max_orders + 1, 10, 1));
    CHECK(ack.status == lob_gateway::ack_rejected);
    CHECK(ack.filled == 0);
    CHECK(f.eng->book_view().bids().aggregate_at(10) == max_orders);
}

TEST_CASE("gateway acks killed for an IOC or FOK that executes nothing", "[gateway]") {
    fixture f;

    // Empty book: an IOC bid and an FOK bid both die without executing.
    CHECK(f.apply(submit(1, 10, 5, 1)).status == lob_gateway::ack_killed);
    CHECK(f.apply(submit(2, 10, 5, 2)).status == lob_gateway::ack_killed);
    CHECK(!f.eng->book_view().bids().best().has_value());

    // A GTC with no fill still acks accepted, because it rests.
    CHECK(f.apply(submit(3, 10, 5, 0)).status == lob_gateway::ack_accepted);

    // An FOK whose precheck fails dies killed, not accepted.
    const lob_gateway::wire_order fok_ask{
        .id = 4, .qty = 50, .px = 10, .new_px = 0, .op = 0, .side = 1, .tif = 2, .pad = 0};
    CHECK(f.apply(fok_ask).status == lob_gateway::ack_killed);
    CHECK(f.eng->book_view().bids().aggregate_at(10) == 5);
}

TEST_CASE("gateway rejects the reserved order ids", "[gateway]") {
    fixture f;

    // Zero is modify's keep-the-id sentinel; 2^64 - 1 is the id_index
    // empty-slot sentinel, which would write a slot that reads as empty and
    // truncate other ids' probe chains.
    const lob_gateway::wire_order zero_id{
        .id = 0, .qty = 5, .px = 10, .new_px = 0, .op = 0, .side = 0, .tif = 0, .pad = 0};
    CHECK(f.apply(zero_id).status == lob_gateway::ack_rejected);

    const lob_gateway::wire_order sentinel_id{.id = ~std::uint64_t{0},
                                              .qty = 5,
                                              .px = 10,
                                              .new_px = 0,
                                              .op = 0,
                                              .side = 0,
                                              .tif = 0,
                                              .pad = 0};
    CHECK(f.apply(sentinel_id).status == lob_gateway::ack_rejected);
    CHECK(!f.eng->book_view().bids().best().has_value());

    // Orders with ordinary ids near the boundary still work, and the book
    // stays reachable by id afterwards.
    CHECK(f.apply(submit(~std::uint64_t{0} - 1, 10, 5)).status == lob_gateway::ack_accepted);
    const lob_gateway::wire_order cxl{.id = ~std::uint64_t{0} - 1,
                                      .qty = 0,
                                      .px = 0,
                                      .new_px = 0,
                                      .op = 1,
                                      .side = 0,
                                      .tif = 0,
                                      .pad = 0};
    CHECK(f.apply(cxl).status == lob_gateway::ack_processed);
    CHECK(!f.eng->book_view().bids().best().has_value());
}
