#ifndef LOB_MESSAGES_HPP
#define LOB_MESSAGES_HPP

#include <lob/types.hpp>

#include <cstdint>
#include <type_traits>

namespace lob {

// ---------- Inbound commands ----------

struct submit_msg {
    order_id_t id;
    tick_t     px;
    qty_t      qty;
    side       s;
    tif        t;
};
static_assert(std::is_trivially_copyable_v<submit_msg>);

struct cancel_msg {
    order_id_t id;
};
static_assert(std::is_trivially_copyable_v<cancel_msg>);

struct modify_msg {
    order_id_t id;
    tick_t     new_px;
    qty_t      new_qty;
};
static_assert(std::is_trivially_copyable_v<modify_msg>);

// ---------- Outbound events ----------

struct fill_msg {
    order_id_t maker;
    order_id_t taker;
    tick_t     px;
    qty_t      qty;
    seq_t      seq;
};
static_assert(std::is_trivially_copyable_v<fill_msg>);

struct top_msg {
    tick_t bid_px;
    tick_t ask_px;
    qty_t  bid_qty;
    qty_t  ask_qty;
    seq_t  seq;
};
static_assert(std::is_trivially_copyable_v<top_msg>);

struct trade_msg {
    tick_t px;
    qty_t  qty;
    seq_t  seq;
};
static_assert(std::is_trivially_copyable_v<trade_msg>);

// ---------- Tagged unions for ring transport ----------

struct command {
    enum class kind : std::uint8_t { submit = 0, cancel = 1, modify = 2 };

    kind k;

    union body_t {
        submit_msg submit;
        cancel_msg cancel;
        modify_msg modify;
        body_t() noexcept : submit{} {}
    } body;

    [[nodiscard]] static command make_submit(submit_msg m) noexcept {
        command c;
        c.k          = kind::submit;
        c.body.submit = m;
        return c;
    }
    [[nodiscard]] static command make_cancel(cancel_msg m) noexcept {
        command c;
        c.k          = kind::cancel;
        c.body.cancel = m;
        return c;
    }
    [[nodiscard]] static command make_modify(modify_msg m) noexcept {
        command c;
        c.k          = kind::modify;
        c.body.modify = m;
        return c;
    }
};
static_assert(std::is_trivially_copyable_v<command>);

struct event {
    enum class kind : std::uint8_t { fill = 0, top = 1, trade = 2 };

    kind k;

    union body_t {
        fill_msg  fill;
        top_msg   top;
        trade_msg trade;
        body_t() noexcept : fill{} {}
    } body;

    [[nodiscard]] static event make_fill(fill_msg m) noexcept {
        event e;
        e.k        = kind::fill;
        e.body.fill = m;
        return e;
    }
    [[nodiscard]] static event make_top(top_msg m) noexcept {
        event e;
        e.k       = kind::top;
        e.body.top = m;
        return e;
    }
    [[nodiscard]] static event make_trade(trade_msg m) noexcept {
        event e;
        e.k         = kind::trade;
        e.body.trade = m;
        return e;
    }
};
static_assert(std::is_trivially_copyable_v<event>);

}  // namespace lob

#endif  // LOB_MESSAGES_HPP
