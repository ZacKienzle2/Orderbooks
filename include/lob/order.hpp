#ifndef LOB_ORDER_HPP
#define LOB_ORDER_HPP

#include <lob/types.hpp>

#include <boost/intrusive/list.hpp>
#include <boost/intrusive/list_hook.hpp>

#include <cstdint>
#include <type_traits>

namespace lob {

// 64-byte cache-line-aligned resting-order record. Layout is hand-packed:
//   id            (8) | remaining (8) | px        (4)
//   side         (1) | tif       (1) | _pad0    (2)
//   level_idx    (4) | _pad1     (4)
//   fifo_hook   (16)                                       -> 48 used
// alignas(64) pads the struct to 64 bytes total; the trailing 16 bytes are
// reserved for future per-order metadata (account id, tags, timestamps).
//
// fifo_hook participates in a boost::intrusive::list that represents the
// FIFO at the order's resting price level. Hook ownership stays with the
// arena that allocated the order; the level borrows it via list::push_back /
// list::erase.
//
// normal_link mode is chosen so the hook stays trivially destructible: the
// engine owns link / unlink timing, and both auto_unlink and safe_link add
// a non-trivial destructor that would force a per-order teardown loop in
// the arena.
struct alignas(64) order {
    order_id_t                                                              id;
    qty_t                                                                   remaining;
    tick_t                                                                  px;
    side                                                                    s;
    tif                                                                     t;
    std::uint16_t                                                           _pad0;
    std::uint32_t                                                           level_idx;
    std::uint32_t                                                           _pad1;
    boost::intrusive::list_member_hook<
        boost::intrusive::link_mode<boost::intrusive::normal_link>>         fifo_hook;
};

static_assert(sizeof(order) == 64);
static_assert(alignof(order) == 64);
static_assert(std::is_trivially_destructible_v<order>);

// constant_time_size<false>: the engine drains a level via the empty() guard
// and front() iteration, never via size(). Skipping the per-link bookkeeping
// shaves a counter update off every push_back / pop_front on the hot path.
// The level aggregate (lob::level::aggregate) is the durable size signal.
using order_fifo = boost::intrusive::list<
    order,
    boost::intrusive::member_hook<order, decltype(order::fifo_hook), &order::fifo_hook>,
    boost::intrusive::constant_time_size<false>>;

}  // namespace lob

#endif  // LOB_ORDER_HPP
