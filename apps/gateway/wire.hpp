#ifndef LOB_GATEWAY_WIRE_HPP
#define LOB_GATEWAY_WIRE_HPP

// Wire protocol for lob_gateway and the decode-validate-dispatch step shared
// by the batched read loop. Kept free of socket headers so the unit tests can
// exercise validation and dispatch without a live connection.

#include <lob/engine.hpp>
#include <lob/messages.hpp>
#include <lob/types.hpp>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace lob_gateway {

// Client -> gateway. Fixed layout, trivially copyable, read straight off the
// socket. op selects the command; new_px is used only by modify.
struct wire_order {
    std::uint64_t id;
    std::uint64_t qty;
    std::uint32_t px;
    std::uint32_t new_px;
    std::uint8_t op;    // 0 submit, 1 cancel, 2 modify
    std::uint8_t side;  // 0 bid, 1 ask
    std::uint8_t tif;   // 0 gtc, 1 ioc, 2 fok
    std::uint8_t pad;
};

static_assert(sizeof(wire_order) == 32);
static_assert(std::is_trivially_copyable_v<wire_order>);

// Gateway -> client. One per order. filled and last_px summarise the order's
// fills; status is 0 accepted, 1 filled, 2 cancel or modify processed, 3
// rejected by validation with the book untouched.
struct wire_ack {
    std::uint64_t id;
    std::uint64_t filled;
    std::uint32_t last_px;
    std::uint32_t status;
};

constexpr std::uint32_t ack_accepted = 0;
constexpr std::uint32_t ack_filled = 1;
constexpr std::uint32_t ack_processed = 2;
constexpr std::uint32_t ack_rejected = 3;

static_assert(sizeof(wire_ack) == 24);
static_assert(std::is_trivially_copyable_v<wire_ack>);

// Accumulates one order's fills so the gateway can summarise them in the ack.
struct accum_pub {
    std::uint64_t filled{0};
    lob::tick_t last_px{0};

    void publish(const lob::fill_msg& f) noexcept {
        filled += f.qty;
        last_px = f.px;
    }

    void publish(const lob::top_msg&) noexcept {}

    void publish(const lob::trade_msg&) noexcept {}

    void publish(const lob::self_trade_msg&) noexcept {}

    void reset() noexcept {
        filled = 0;
        last_px = 0;
    }
};

// Validates one wire_order against the engine's tick ladder before dispatch.
// The engine's hot path treats out-of-range px as UB (the tick ladder is
// indexed unchecked and the bitmap guard is an assert compiled out under
// NDEBUG), and casting an arbitrary byte to lob::tif is UB before any switch
// sees it, so every field a command consumes is range-checked here. Rejected
// orders are refused outright rather than clamped, since clamping would
// silently reprice the client's order.
template <std::size_t Ticks>
[[nodiscard]] bool validate_order(const wire_order& wo) noexcept {
    switch (wo.op) {
    case 0:
        return wo.px < Ticks && wo.qty > 0 && wo.tif <= static_cast<std::uint8_t>(lob::tif::fok);
    case 1:
        return true;  // cancel consumes only the id
    default:
        return wo.new_px < Ticks && wo.qty > 0;
    }
}

// Decode one wire_order into its command, run it on the engine, and fill the
// ack. Shared by the batched read loop so the dispatch lives in one place.
// Invalid orders never reach the engine; they ack ack_rejected instead.
template <std::size_t Ticks, std::size_t MaxOrders>
void apply_order(lob::engine<accum_pub, Ticks, MaxOrders>& eng,
                 accum_pub& pub,
                 const wire_order& wo,
                 wire_ack& ack) noexcept {
    pub.reset();
    if (!validate_order<Ticks>(wo)) {
        ack = wire_ack{.id = wo.id, .filled = 0, .last_px = 0, .status = ack_rejected};
        return;
    }
    std::uint32_t status = 0;
    switch (wo.op) {
    case 0:
        eng.on_submit(lob::submit_msg{.id = wo.id,
                                      .px = wo.px,
                                      .qty = wo.qty,
                                      .s = wo.side == 0 ? lob::side::bid : lob::side::ask,
                                      .t = static_cast<lob::tif>(wo.tif),
                                      ._pad = 0,
                                      .account_id = 0});
        status = pub.filled > 0 ? ack_filled : ack_accepted;
        break;
    case 1:
        eng.on_cancel(lob::cancel_msg{.id = wo.id});
        status = ack_processed;
        break;
    default:
        eng.on_modify(lob::modify_msg{.id = wo.id, .new_px = wo.new_px, .new_qty = wo.qty});
        status = ack_processed;
        break;
    }
    ack = wire_ack{.id = wo.id, .filled = pub.filled, .last_px = pub.last_px, .status = status};
}

}  // namespace lob_gateway

#endif  // LOB_GATEWAY_WIRE_HPP
