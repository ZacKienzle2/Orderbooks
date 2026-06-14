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
// Dense per-side ladder of `Ticks` price levels backed by a hierarchical
// bitmap. best() reads a cached top-of-book maintained incrementally by add /
// remove, so it never descends the bitmap on the hot path. The bitmap remains
// the source of truth for next / prev_populated and refreshes the cache when
// the top level drains. Side is a non-type template parameter, so the best()
// direction is dead-coded per instantiation (highest_set for bids, lowest_set
// for asks).
//
// add / remove update one level and at most one bitmap word per tier; remove
// pays a single bitmap descent only when the top level empties. Both are
// noexcept; out-of-range tick indices are UB-by-contract (engine validates
// upstream).
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
        if (was_empty)
            bm_.set(o.px);
        // A newly resting price can only equal or improve this side's best,
        // so one compare maintains the cache with no bitmap descent.
        if (!has_best_) {
            best_ = o.px;
            has_best_ = true;
        } else if constexpr (Side == side::bid) {
            if (o.px > best_)
                best_ = o.px;
        } else {
            if (o.px < best_)
                best_ = o.px;
        }
    }

    void remove(order& o) noexcept {
        auto& lvl = (*levels_)[o.px];
        lvl.unlink(o);
        if (lvl.empty()) {
            bm_.clear(o.px);
            // The best moves only when the top level itself drains; a deeper
            // level emptying leaves the cache valid. The descent in
            // recompute_best_ therefore runs only on the top-of-book case.
            if (o.px == best_)
                recompute_best_();
        }
    }

    // O(1) best-price query returning the incrementally maintained cache. The
    // bitmap stays the source of truth for next / prev_populated and for
    // refreshing this cache when the top level drains.
    [[nodiscard]] std::optional<tick_t> best() const noexcept {
        if (!has_best_)
            return std::nullopt;
        return best_;
    }

    [[nodiscard]] qty_t aggregate_at(tick_t px) const noexcept { return (*levels_)[px].aggregate; }

    [[nodiscard]] const level& level_at(tick_t px) const noexcept { return (*levels_)[px]; }

    [[nodiscard]] level& level_at(tick_t px) noexcept { return (*levels_)[px]; }

    // Single chokepoint through which a match-path drain clears the bitmap.
    // The engine drains a level FIFO directly during a cross and calls this
    // once the level has emptied. Refresh the cached best here when the
    // drained level was the top of book.
    void notify_level_emptied(tick_t px) noexcept {
        bm_.clear(px);
        if (has_best_ && px == best_)
            recompute_best_();
    }

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
    // Recompute the cached best from the bitmap. Runs only when the top level
    // drains (remove / notify_level_emptied), so its descent is amortised
    // across the O(1) best() reads it enables everywhere else.
    void recompute_best_() noexcept {
        const auto v = (Side == side::bid) ? bm_.highest_set() : bm_.lowest_set();
        has_best_ = v.has_value();
        best_ = has_best_ ? static_cast<tick_t>(*v) : tick_t{0};
    }

    std::unique_ptr<std::array<level, Ticks>> levels_;
    hier_bitmap<Ticks> bm_{};
    tick_t best_{0};
    bool has_best_{false};
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

    // Regression guard. If a future change strips one of the alignas(64)
    // markers above, the static_assert fires loudly at instantiation.
    static_assert(alignof(book_side<Ticks, side::bid>) >= 64,
                  "book::bids_ must be cache-line aligned");
    static_assert(alignof(book_side<Ticks, side::ask>) >= 64,
                  "book::asks_ must be cache-line aligned");
};

}  // namespace lob

#endif  // LOB_BOOK_HPP
