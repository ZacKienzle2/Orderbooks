#ifndef LOB_BOOK_HPP
#define LOB_BOOK_HPP

#include <lob/arena.hpp>
#include <lob/bitmap.hpp>
#include <lob/id_index.hpp>
#include <lob/level.hpp>
#include <lob/order.hpp>
#include <lob/types.hpp>

#include <array>
#include <cstddef>
#include <memory>
#include <optional>

namespace lob {

// book_side<Ticks, Side>
// ----------------------
// Dense per-side ladder of `Ticks` price levels plus a hierarchical bitmap
// for `O(1)` best-price queries. Side is encoded as a non-type template
// parameter so the `best()` direction is dead-coded per instantiation
// (highest_set for bids, lowest_set for asks).
//
// add / remove update one level and at most one bitmap word per tier. Both
// are noexcept; out-of-range tick indices are UB-by-contract (the engine
// validates upstream).
template <std::size_t Ticks, side Side>
class book_side {
  public:
    book_side() : levels_(std::make_unique<std::array<level, Ticks>>()) {}

    book_side(book_side&&) noexcept = default;
    book_side& operator=(book_side&&) noexcept = default;
    book_side(const book_side&) = delete;
    book_side& operator=(const book_side&) = delete;
    ~book_side() = default;

    void add(order& o) noexcept {
        auto& lvl = (*levels_)[o.px];
        const bool was_empty = lvl.empty();
        lvl.push_back(o);
        o.level_idx = o.px;
        if (was_empty)
            bm_.set(o.px);
    }

    void remove(order& o) noexcept {
        auto& lvl = (*levels_)[o.level_idx];
        lvl.unlink(o);
        if (lvl.empty())
            bm_.clear(o.level_idx);
    }

    [[nodiscard]] std::optional<tick_t> best() const noexcept {
        const auto v = (Side == side::bid) ? bm_.highest_set() : bm_.lowest_set();
        if (!v.has_value())
            return std::nullopt;
        return static_cast<tick_t>(*v);
    }

    [[nodiscard]] qty_t aggregate_at(tick_t px) const noexcept { return (*levels_)[px].aggregate; }

    [[nodiscard]] const level& level_at(tick_t px) const noexcept { return (*levels_)[px]; }

    [[nodiscard]] level& level_at(tick_t px) noexcept { return (*levels_)[px]; }

    // The matching engine drains a level FIFO directly during a cross and
    // needs to inform the bitmap when the level emptied as a result.
    void notify_level_emptied(tick_t px) noexcept { bm_.clear(px); }

    // Inspect the bitmap for FOK precheck without exposing internals.
    [[nodiscard]] std::optional<tick_t> next_populated_at_or_after(tick_t px) const noexcept {
        const auto v = bm_.next_set_at_or_after(px);
        if (!v.has_value())
            return std::nullopt;
        return static_cast<tick_t>(*v);
    }

    [[nodiscard]] std::optional<tick_t> prev_populated_at_or_before(tick_t px) const noexcept {
        const auto v = bm_.prev_set_at_or_before(px);
        if (!v.has_value())
            return std::nullopt;
        return static_cast<tick_t>(*v);
    }

    [[nodiscard]] bool empty() const noexcept { return bm_.empty(); }

    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Ticks; }

  private:
    std::unique_ptr<std::array<level, Ticks>> levels_;
    hier_bitmap<Ticks> bm_{};
};

// book<Ticks, MaxOrders>
// ----------------------
// Aggregates the bid and ask sides, the slab arena for `order` storage, and
// the id_index for cancel / modify lookup. The book is the unit of state
// the engine mutates; it is single-symbol by design.
template <std::size_t Ticks, std::size_t MaxOrders>
class book {
  public:
    book() : idx_(MaxOrders) {}

    book(book&&) = default;
    book& operator=(book&&) = default;
    book(const book&) = delete;
    book& operator=(const book&) = delete;
    ~book() = default;

    [[nodiscard]] book_side<Ticks, side::bid>& bids() noexcept { return bids_; }

    [[nodiscard]] const book_side<Ticks, side::bid>& bids() const noexcept { return bids_; }

    [[nodiscard]] book_side<Ticks, side::ask>& asks() noexcept { return asks_; }

    [[nodiscard]] const book_side<Ticks, side::ask>& asks() const noexcept { return asks_; }

    [[nodiscard]] slab_arena<order, MaxOrders>& arena() noexcept { return arena_; }

    [[nodiscard]] id_index& index() noexcept { return idx_; }

    [[nodiscard]] const id_index& index() const noexcept { return idx_; }

  private:
    // Align each side to a 64-byte boundary so the hot fields (the
    // unique_ptr to the level array and the bitmap's top-tier word)
    // of bids_ and asks_ never share a cache line. Without this, a
    // bid-side update could invalidate the cache line carrying the
    // ask-side cursor data and vice versa under a future consumer /
    // producer split between the two sides.
    alignas(64) book_side<Ticks, side::bid> bids_;
    alignas(64) book_side<Ticks, side::ask> asks_;
    alignas(64) slab_arena<order, MaxOrders> arena_;
    id_index idx_;
};

}  // namespace lob

#endif  // LOB_BOOK_HPP
