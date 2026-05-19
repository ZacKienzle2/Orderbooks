#ifndef LOB_LEVEL_HPP
#define LOB_LEVEL_HPP

#include <lob/order.hpp>
#include <lob/types.hpp>

#include <cstddef>

namespace lob {

// A single price level on one side of the book. Owns an intrusive FIFO of
// orders resting at this price plus a cached aggregate quantity so the
// engine reads top-of-level qty without walking the list.
//
// The aggregate is maintained as orders are linked or unlinked; it is the
// caller's responsibility to keep it in sync (the engine drives this).
//
// `level` is not trivially copyable because it embeds an intrusive list.
// It is default-constructible into an empty state and is intended to live
// in a dense `std::array<level, Ticks>` inside the book side.
struct level {
    qty_t      aggregate{0};
    order_fifo fifo{};

    [[nodiscard]] bool empty() const noexcept { return fifo.empty(); }

    // order_count is O(orders-at-this-level); the engine never calls it on
    // the hot path. Use aggregate for the durable quantity signal; this
    // exists only for tests and diagnostics.
    [[nodiscard]] std::size_t order_count() const noexcept { return fifo.size(); }

    void push_back(order& o) noexcept {
        fifo.push_back(o);
        aggregate += o.remaining;
    }

    void unlink(order& o) noexcept {
        aggregate -= o.remaining;
        fifo.erase(fifo.iterator_to(o));
    }

    void clear() noexcept {
        aggregate = 0;
        fifo.clear();
    }
};

}  // namespace lob

#endif  // LOB_LEVEL_HPP
