#ifndef LOB_ORDER_HPP
#define LOB_ORDER_HPP

#include <lob/types.hpp>

#include <boost/intrusive/list.hpp>
#include <boost/intrusive/list_hook.hpp>

#include <cstdint>
#include <type_traits>

namespace lob {

// 64-byte cache-line-aligned resting-order record. Byte layout follows.
//   id            (8) | remaining (8) | px         (4)
//   side         (1) | tif       (1) | _pad0     (2)
//   account_id   (4)
//   fifo_hook   (16)                                       -> 44 used
// alignas(64) pads the struct to 64 bytes total; the trailing bytes are
// reserved for future per-order metadata (tags, timestamps).
//
// The resting price level is `px` itself. An order is only ever removed from
// the level it rests at, and every remove() site holds `px` unchanged at the
// resting value, so a separate cached level index would be pure redundancy.
//
// fifo_hook participates in a boost::intrusive::list that represents the
// FIFO at the order's resting price level. Hook ownership stays with the
// arena that allocated the order; the level borrows it via list::push_back /
// list::erase.
//
// normal_link mode keeps the hook trivially destructible. The engine owns
// link / unlink timing, and both auto_unlink and safe_link add a non-trivial
// destructor that would force a per-order teardown loop in the arena.
struct alignas(64) order {
    order_id_t id;
    qty_t remaining;
    tick_t px;
    side s;
    tif t;
    std::uint16_t _pad0;
    account_id_t account_id;
    boost::intrusive::list_member_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>>
        fifo_hook;
};

static_assert(sizeof(order) == 64);
static_assert(alignof(order) == 64);
// On Apple libc++ the boost::intrusive::list_member_hook with normal_link mode
// is trivially destructible; on GNU libstdc++ it is not. slab_arena calls
// ~order() on deallocate, which is a no-op for the hook in normal_link mode.

// With constant_time_size<false> the engine drains a level via the empty()
// guard and front() iteration, never via size(). Skipping the per-link
// bookkeeping shaves a counter update off every push_back / pop_front on the
// hot path. The level aggregate (lob::level::aggregate) is the durable size
// signal.
using order_fifo = boost::intrusive::list<
    order,
    boost::intrusive::member_hook<order, decltype(order::fifo_hook), &order::fifo_hook>,
    boost::intrusive::constant_time_size<false>>;

}  // namespace lob

#endif  // LOB_ORDER_HPP
