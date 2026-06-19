#ifndef LOB_ENGINE_HPP
#define LOB_ENGINE_HPP

#include <lob/book.hpp>
#include <lob/concepts.hpp>
#include <lob/config.hpp>
#include <lob/messages.hpp>
#include <lob/order.hpp>
#include <lob/snapshot.hpp>
#include <lob/types.hpp>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

namespace lob {

// engine<P, Ticks, MaxOrders>
// ---------------------------
// Single-symbol matching engine. The engine owns a book and a sequence
// counter; it does not own the publisher (caller manages its lifetime).
//
// Hot-path methods (on_submit, on_cancel, on_modify) are noexcept by
// contract. The gateway is responsible for upstream validation of px, qty,
// side, tif; the engine treats out-of-range inputs as UB.
//
// Matching follows strict price-time priority. When an aggressor crosses
// the opposite best, the engine walks that level's FIFO from the front,
// consuming maker orders until the aggressor is filled or the price stops
// crossing. Each fill emits a fill_msg with a monotonic seq. Top-of-book
// changes emit a top_msg subject to engine_config::top_throttle.
template <publisher P, std::size_t Ticks, std::size_t MaxOrders>
class engine {
    // Belt-and-suspenders enforcement of the publisher::publish noexcept
    // contract. The publisher concept already constrains these overloads,
    // but the static_asserts make the assumption explicit at the engine's
    // own instantiation point so any future relaxation of the concept (or
    // a non-concept-checked instantiation, e.g. through type erasure) fails
    // here rather than silently terminating mid-match if a publish ever
    // throws while the book is in a transient state.
    static_assert(noexcept(std::declval<P&>().publish(std::declval<const fill_msg&>())),
                  "publisher::publish(fill_msg) must be noexcept");
    static_assert(noexcept(std::declval<P&>().publish(std::declval<const top_msg&>())),
                  "publisher::publish(top_msg) must be noexcept");
    static_assert(noexcept(std::declval<P&>().publish(std::declval<const trade_msg&>())),
                  "publisher::publish(trade_msg) must be noexcept");
    static_assert(noexcept(std::declval<P&>().publish(std::declval<const self_trade_msg&>())),
                  "publisher::publish(self_trade_msg) must be noexcept");

  public:
    engine(P& pub, engine_config cfg) noexcept : pub_(pub), cfg_(cfg) {}

    engine(const engine&) = delete;
    engine(engine&&) = delete;
    engine& operator=(const engine&) = delete;
    engine& operator=(engine&&) = delete;
    ~engine() = default;

    [[gnu::hot]] void on_submit(const submit_msg& m) noexcept {
        if (m.s == side::bid)
            handle_submit_<side::bid>(m);
        else
            handle_submit_<side::ask>(m);
        publish_top_if_changed_();
    }

    [[gnu::hot]] void on_cancel(const cancel_msg& m) noexcept {
        auto* o = book_.index().lookup(m.id);
        if (o == nullptr)
            return;
        // The order's hot fields (s, px, remaining, account_id) and the
        // FIFO hook share the order's cache line. on_cancel mutates that
        // line (remove unlinks the hook; deallocate writes the freelist
        // link), so issue a write-prefetch (rw=1) with T0 locality so the
        // line lands in L1 already in Modified state and the upcoming
        // RFO upgrade is skipped.
        __builtin_prefetch(o, 1, 3);
        const auto cancel_side = o->s;
        const auto cancel_px = o->px;
        if (o->s == side::bid)
            book_.bids().remove(*o);
        else
            book_.asks().remove(*o);
        book_.index().erase(o->id);
        book_.arena().deallocate(o);
        mark_top_(cancel_side, cancel_px);
        publish_top_if_changed_();
    }

    [[gnu::hot]] void on_modify(const modify_msg& m) noexcept {
        auto* o = book_.index().lookup(m.id);
        if (o == nullptr)
            return;
        // Issue the write-prefetch first so the line is in flight while
        // the branch below resolves; the field reads then hit cache
        // instead of paying the miss latency synchronously. Modify
        // mutates remaining and (on price change) reroutes the FIFO
        // hook, so the write hint (rw=1) avoids an RFO upgrade later.
        __builtin_prefetch(o, 1, 3);
        const auto s = o->s;
        const auto t = o->t;
        if (m.new_px == o->px) {
            if (m.new_qty == o->remaining)
                return;  // genuine no-op
            // qty-only fast path: mutate aggregate in place
            if (s == side::bid) {
                auto& lvl = book_.bids().level_at(o->px);
                lvl.aggregate = lvl.aggregate - o->remaining + m.new_qty;
            } else {
                auto& lvl = book_.asks().level_at(o->px);
                lvl.aggregate = lvl.aggregate - o->remaining + m.new_qty;
            }
            o->remaining = m.new_qty;
            mark_top_(s, o->px);
            publish_top_if_changed_();
            return;
        }
        // Price change that still rests: relink the existing record in place.
        // The order survives, so its id_index entry and arena slot are
        // unchanged; only the level FIFOs and bitmaps move. This skips the
        // index erase and insert and the arena free and allocate a cancel and
        // resubmit pays, the two costliest random-memory steps on this path. A
        // price change forfeits time priority either way, so landing at the
        // back of the new level reproduces the cancel-and-resubmit result.
        if (s == side::bid) {
            const auto best_ask = book_.asks().best();
            if (!best_ask.has_value() || m.new_px < *best_ask) {
                const auto old_px = o->px;
                book_.bids().remove(*o);
                o->px = m.new_px;
                o->remaining = m.new_qty;
                book_.bids().add(*o);
                mark_top_(side::bid, old_px);
                mark_top_(side::bid, m.new_px);
                publish_top_if_changed_();
                return;
            }
        } else {
            const auto best_bid = book_.bids().best();
            if (!best_bid.has_value() || m.new_px > *best_bid) {
                const auto old_px = o->px;
                book_.asks().remove(*o);
                o->px = m.new_px;
                o->remaining = m.new_qty;
                book_.asks().add(*o);
                mark_top_(side::ask, old_px);
                mark_top_(side::ask, m.new_px);
                publish_top_if_changed_();
                return;
            }
        }
        // Crossing price change: cancel + resubmit at the new price so the
        // order matches against the book and loses time priority. Suppress
        // nested top_msg emission so the modify appears as a single coalesced
        // top change rather than one per sub-step.
        const auto acct = o->account_id;
        assert(state_.suppress_top_depth < std::numeric_limits<std::uint8_t>::max() &&
               "engine: suppress_top_depth would overflow; composite nesting too deep");
        ++state_.suppress_top_depth;
        on_cancel(cancel_msg{.id = m.id});
        on_submit(submit_msg{.id = m.id,
                             .px = m.new_px,
                             .qty = m.new_qty,
                             .s = s,
                             .t = t,
                             ._pad = 0,
                             .account_id = acct});
        --state_.suppress_top_depth;
        publish_top_if_changed_();
    }

    // Serialise the engine's complete state into a snapshot_sink.
    //
    // Layout: a snapshot_header followed by num_orders snapshot_order_records.
    // Records are emitted in (price ascending, FIFO front-to-back) order per
    // side, bids before asks. That order is the same order restore() needs
    // to replay them in to reproduce the FIFO time priority at each level.
    //
    // Throwing: snapshot() propagates any exception thrown by the sink's
    // write(). Production sinks (fixed buffers, mmap-backed files, ring
    // publishers) should mark write() noexcept; growable sinks like
    // vector_snapshot_buffer can throw std::bad_alloc on memory pressure
    // and the engine state is unaffected because snapshot() never mutates.
    template <snapshot_sink S>
    [[gnu::cold]] void snapshot(S& sink) const {
        snapshot_header hdr{};
        hdr.ticks = Ticks;
        hdr.max_orders = MaxOrders;
        hdr.self_cross = static_cast<std::uint8_t>(cfg_.self_cross);
        hdr.top_throttle = cfg_.top_throttle ? std::uint8_t{1} : std::uint8_t{0};
        hdr.seq = state_.seq;
        hdr.last_bid_px = state_.last_bid_px;
        hdr.last_ask_px = state_.last_ask_px;
        hdr.last_bid_qty = state_.last_bid_qty;
        hdr.last_ask_qty = state_.last_ask_qty;
        hdr.have_top = state_.have_top ? std::uint8_t{1} : std::uint8_t{0};
        hdr.num_orders = count_resting_();
        emit_bytes_(sink, &hdr, sizeof(hdr));

        emit_side_<side::bid>(sink);
        emit_side_<side::ask>(sink);
    }

    // Restore engine state from a snapshot_source. Clears any existing
    // state first. Returns false if the header magic / version / template
    // parameters do not match the engine instance, leaving the engine in a
    // freshly cleared state.
    template <snapshot_source R>
    [[nodiscard]] [[gnu::cold]] bool restore(R& src) noexcept {
        snapshot_header hdr{};
        if (!read_bytes_(src, &hdr, sizeof(hdr))) {
            clear_state_();
            return false;
        }
        if (hdr.magic != snapshot_header::magic_bytes)
            return clear_state_and_fail_();
        if (hdr.version != snapshot_header::wire_version)
            return clear_state_and_fail_();
        if (hdr.ticks != Ticks)
            return clear_state_and_fail_();
        if (hdr.max_orders != MaxOrders)
            return clear_state_and_fail_();

        clear_state_();
        cfg_.self_cross = static_cast<self_cross_policy>(hdr.self_cross);
        cfg_.top_throttle = hdr.top_throttle != 0;
        state_.seq = hdr.seq;
        state_.last_bid_px = hdr.last_bid_px;
        state_.last_ask_px = hdr.last_ask_px;
        state_.last_bid_qty = hdr.last_bid_qty;
        state_.last_ask_qty = hdr.last_ask_qty;
        state_.have_top = hdr.have_top != 0;
        state_.top_dirty = false;

        for (std::uint64_t i = 0; i < hdr.num_orders; ++i) {
            snapshot_order_record rec{};
            if (!read_bytes_(src, &rec, sizeof(rec))) {
                clear_state_();
                return false;
            }
            if (!replay_record_(rec)) {
                clear_state_();
                return false;
            }
        }
        // The replayed book is now whole, so seed the per-side presence flags
        // from it and leave the top clean, matching the no-emit-on-restore
        // contract.
        state_.have_bid_side = book_.bids().best().has_value();
        state_.have_ask_side = book_.asks().best().has_value();
        state_.top_dirty = false;
        return true;
    }

    [[nodiscard]] const book<Ticks, MaxOrders>& book_view() const noexcept { return book_; }

    [[nodiscard]] seq_t last_seq() const noexcept { return state_.seq; }

  private:
    template <side Side>
    void handle_submit_(const submit_msg& m) noexcept {
        constexpr auto Opp = (Side == side::bid) ? side::ask : side::bid;

        // FOK precheck: walk opposite levels from best toward `m.px` and sum
        // their aggregates; abort all if the total is less than the request.
        if (m.t == tif::fok) {
            if (!can_fully_fill_<Opp>(m.px, m.qty))
                return;
        }

        qty_t remaining = m.qty;
        match_against_opposite_<Side>(m, remaining);

        if (remaining == 0)
            return;

        switch (m.t) {
        case tif::ioc:
        case tif::fok:
            // IOC always drops the residual. FOK never reaches here with
            // residual > 0 unless we crossed multiple levels and lost
            // qty to rounding (impossible with integer qty); drop too.
            return;
        case tif::gtc:
        default:
            rest_<Side>(m, remaining);
            return;
        }
    }

    template <side Side>
    void match_against_opposite_(const submit_msg& m, qty_t& remaining) noexcept {
        constexpr auto Opp = (Side == side::bid) ? side::ask : side::bid;
        auto& opp = side_<Opp>();

        while (remaining > 0) {
            const auto best = opp.best();
            if (!best.has_value())
                return;
            const auto best_px = *best;
            if constexpr (Side == side::bid) {
                if (m.px < best_px)
                    return;
            } else {
                if (m.px > best_px)
                    return;
            }

            auto& lvl = opp.level_at(best_px);
            while (remaining > 0 && !lvl.empty()) {
                auto& maker = lvl.fifo.front();

                if (m.account_id != 0 && maker.account_id == m.account_id) {
                    if (handle_self_cross_(m, maker, lvl, best_px, remaining)) {
                        if (lvl.empty())
                            opp.notify_level_emptied(best_px);
                        return;  // cancel_newest path: abort the aggressor entirely
                    }
                    continue;  // cancel_oldest / decrement_trade: re-check loop guard
                }

                const auto trade_qty = std::min(remaining, maker.remaining);

                ++state_.seq;
                pub_.publish(fill_msg{
                    .maker = maker.id,
                    .taker = m.id,
                    .px = best_px,
                    .qty = trade_qty,
                    .seq = state_.seq,
                });
                pub_.publish(trade_msg{.px = best_px, .qty = trade_qty, .seq = state_.seq});

                maker.remaining -= trade_qty;
                lvl.aggregate -= trade_qty;
                remaining -= trade_qty;
                state_.top_dirty = true;

                if (maker.remaining == 0) {
                    auto* victim = &maker;
                    const auto victim_id = victim->id;
                    lvl.fifo.pop_front();
                    book_.index().erase(victim_id);
                    book_.arena().deallocate(victim);
                }
            }

            if (lvl.empty())
                opp.notify_level_emptied(best_px);
        }
    }

    // Dispatches the configured self_cross_policy when the aggressor would
    // match against a maker from the same account. Returns true when the
    // aggressor itself must abort (cancel_newest); returns false when the
    // outer match loop should re-evaluate the level (cancel_oldest after
    // removing the maker; decrement_trade after netting both sides).
    bool handle_self_cross_(
        const submit_msg& m, order& maker, level& lvl, tick_t best_px, qty_t& remaining) noexcept {
        switch (cfg_.self_cross) {
        case self_cross_policy::cancel_newest:
            remaining = 0;
            return true;
        case self_cross_policy::cancel_oldest: {
            auto* victim = &maker;
            const auto victim_id = victim->id;
            lvl.aggregate -= maker.remaining;
            lvl.fifo.pop_front();
            book_.index().erase(victim_id);
            book_.arena().deallocate(victim);
            state_.top_dirty = true;
            return false;
        }
        case self_cross_policy::decrement_trade: {
            const auto trade_qty = std::min(remaining, maker.remaining);
            ++state_.seq;
            pub_.publish(self_trade_msg{
                .aggressor = m.id,
                .resting = maker.id,
                .account = m.account_id,
                .px = best_px,
                .qty = trade_qty,
                .seq = state_.seq,
            });
            maker.remaining -= trade_qty;
            lvl.aggregate -= trade_qty;
            remaining -= trade_qty;
            state_.top_dirty = true;
            if (maker.remaining == 0) {
                auto* victim = &maker;
                const auto victim_id = victim->id;
                lvl.fifo.pop_front();
                book_.index().erase(victim_id);
                book_.arena().deallocate(victim);
            }
            return false;
        }
        }
        return false;
    }

    template <side Side>
    void rest_(const submit_msg& m, qty_t remaining) noexcept {
        auto* o = book_.arena().allocate();
        if (o == nullptr)
            return;  // arena exhausted; gateway-side concern
        o->id = m.id;
        o->remaining = remaining;
        o->px = m.px;
        o->s = Side;
        o->t = m.t;
        o->_pad0 = 0;
        o->level_idx = m.px;
        o->account_id = m.account_id;
        side_<Side>().add(*o);
        book_.index().insert(m.id, o);
        mark_top_(Side, m.px);
    }

    template <side Opp>
    [[nodiscard]] bool can_fully_fill_(tick_t aggressor_px, qty_t want) const noexcept {
        // Walk opposite-side levels from best toward aggressor_px, summing
        // their aggregates. For bid aggressor we walk ask levels from
        // lowest toward aggressor_px (inclusive). For ask aggressor we walk
        // bid levels from highest toward aggressor_px (inclusive).
        qty_t total = 0;
        if constexpr (Opp == side::ask) {
            // aggressor is bid; walk asks ascending from best to aggressor_px
            auto px = book_.asks().best();
            while (px.has_value() && *px <= aggressor_px) {
                total += book_.asks().aggregate_at(*px);
                if (total >= want)
                    return true;
                px = book_.asks().next_populated_at_or_after(*px + 1);
            }
        } else {
            // aggressor is ask; walk bids descending from best to aggressor_px
            auto px = book_.bids().best();
            while (px.has_value() && *px >= aggressor_px) {
                total += book_.bids().aggregate_at(*px);
                if (total >= want)
                    return true;
                if (*px == 0)
                    break;
                px = book_.bids().prev_populated_at_or_before(*px - 1);
            }
        }
        return total >= want;
    }

    // Conservatively reports whether a mutation at price px on side s could
    // move top-of-book. A change strictly worse than the side's current best,
    // on a side that already held orders at the last top, cannot, because the
    // best price and the best level's quantity are untouched. Those skip the
    // best recompute in publish_top_if_changed_. When no top is established, or
    // the side was empty at the last top, any change there may set the top.
    [[nodiscard]] bool affects_top_(side s, tick_t px) const noexcept {
        if (!state_.have_top)
            return true;
        if (s == side::bid)
            return !state_.have_bid_side || px >= state_.last_bid_px;
        return !state_.have_ask_side || px <= state_.last_ask_px;
    }

    // Marks the top dirty after a mutation at price px on side s. With throttle
    // off the engine emits a top per mutation by contract, so the guard applies
    // only when throttle is on, where it suppresses the best recompute for a
    // mutation that cannot move the top.
    void mark_top_(side s, tick_t px) noexcept {
        if (!cfg_.top_throttle || affects_top_(s, px))
            state_.top_dirty = true;
    }

    void publish_top_if_changed_() noexcept {
        if (!state_.top_dirty)
            return;
        // Honour suppression set by composite operations; keep top_dirty
        // set so the outermost publish (after the composite completes)
        // observes the cumulative state change and emits once.
        if (state_.suppress_top_depth != 0)
            return;
        state_.top_dirty = false;

        const auto bb = book_.bids().best();
        const auto ba = book_.asks().best();

        const tick_t bid_px = bb.has_value() ? *bb : tick_t{0};
        const tick_t ask_px = ba.has_value() ? *ba : tick_t{0};
        const qty_t bid_qty = bb.has_value() ? book_.bids().aggregate_at(*bb) : qty_t{0};
        const qty_t ask_qty = ba.has_value() ? book_.asks().aggregate_at(*ba) : qty_t{0};

        // Refresh the per-side presence flags whenever the best is recomputed,
        // before any throttle early-return, so affects_top_ never reads a stale
        // flag.
        state_.have_bid_side = bb.has_value();
        state_.have_ask_side = ba.has_value();

        if (cfg_.top_throttle && state_.have_top && bid_px == state_.last_bid_px &&
            ask_px == state_.last_ask_px && bid_qty == state_.last_bid_qty &&
            ask_qty == state_.last_ask_qty) {
            return;
        }

        ++state_.seq;
        pub_.publish(top_msg{
            .bid_px = bid_px,
            .ask_px = ask_px,
            .bid_qty = bid_qty,
            .ask_qty = ask_qty,
            .seq = state_.seq,
        });
        state_.last_bid_px = bid_px;
        state_.last_ask_px = ask_px;
        state_.last_bid_qty = bid_qty;
        state_.last_ask_qty = ask_qty;
        state_.have_top = true;
    }

    template <side S>
    [[nodiscard]] book_side<Ticks, S>& side_() noexcept {
        if constexpr (S == side::bid)
            return book_.bids();
        else
            return book_.asks();
    }

    [[nodiscard]] [[gnu::cold]] std::uint64_t count_resting_() const noexcept {
        // Drive the walk from the bitmap so empty tiers cost nothing.
        // count_resting_ is cold (called once per snapshot()), but the
        // linear O(Ticks) scan was wasteful on sparse books with large
        // Ticks. Bitmap descent collapses the empty bulk to a tier walk.
        std::uint64_t count = 0;
        for (auto px = book_.bids().next_populated_at_or_after(0); px.has_value();
             px = (*px == Ticks - 1) ? std::nullopt
                                     : book_.bids().next_populated_at_or_after(*px + 1)) {
            for (const auto& o : book_.bids().level_at(*px).fifo) {
                (void)o;
                ++count;
            }
        }
        for (auto px = book_.asks().next_populated_at_or_after(0); px.has_value();
             px = (*px == Ticks - 1) ? std::nullopt
                                     : book_.asks().next_populated_at_or_after(*px + 1)) {
            for (const auto& o : book_.asks().level_at(*px).fifo) {
                (void)o;
                ++count;
            }
        }
        return count;
    }

    template <snapshot_sink S>
    static void emit_bytes_(S& sink, const void* p, std::size_t n) {
        sink.write(std::span<const std::byte>{static_cast<const std::byte*>(p), n});
    }

    template <snapshot_source R>
    static bool read_bytes_(R& src, void* p, std::size_t n) noexcept {
        return src.read(std::span<std::byte>{static_cast<std::byte*>(p), n});
    }

    template <side Side, snapshot_sink S>
    void emit_side_(S& sink) const {
        // Snapshot contract: records are emitted in (price ascending,
        // FIFO front-to-back) order so restore() replays them in the
        // same order and reproduces FIFO time priority at each level.
        // The iteration is driven by the bitmap (always ascending via
        // next_populated_at_or_after) regardless of which side is best
        // at the high or low end of the ladder.
        auto emit_from = [&](auto& side) {
            for (auto px = side.next_populated_at_or_after(0); px.has_value();
                 px = (*px == Ticks - 1) ? std::nullopt
                                         : side.next_populated_at_or_after(*px + 1)) {
                for (const auto& o : side.level_at(*px).fifo) {
                    snapshot_order_record rec{};
                    rec.id = o.id;
                    rec.remaining = o.remaining;
                    rec.px = o.px;
                    rec.s = static_cast<std::uint8_t>(Side);
                    rec.t = static_cast<std::uint8_t>(o.t);
                    rec.account_id = o.account_id;
                    emit_bytes_(sink, &rec, sizeof(rec));
                }
            }
        };
        if constexpr (Side == side::bid)
            emit_from(book_.bids());
        else
            emit_from(book_.asks());
    }

    [[gnu::cold]] bool clear_state_and_fail_() noexcept {
        clear_state_();
        return false;
    }

    [[gnu::cold]] void clear_state_() noexcept {
        // Drain both sides, releasing every live order back to the arena.
        // Drive the iteration from the bitmap so empty tiers cost nothing.
        // remove() updates the bitmap as it goes, so re-querying best()
        // each outer iteration is the cheapest way to keep up with the
        // shrinking populated set.
        while (auto px = book_.bids().best()) {
            auto& lvl = book_.bids().level_at(*px);
            while (!lvl.fifo.empty()) {
                auto& o = lvl.fifo.front();
                auto* victim = &o;
                book_.bids().remove(o);
                book_.index().erase(victim->id);
                book_.arena().deallocate(victim);
            }
        }
        while (auto px = book_.asks().best()) {
            auto& lvl = book_.asks().level_at(*px);
            while (!lvl.fifo.empty()) {
                auto& o = lvl.fifo.front();
                auto* victim = &o;
                book_.asks().remove(o);
                book_.index().erase(victim->id);
                book_.arena().deallocate(victim);
            }
        }
        state_ = hot_state{};
    }

    [[gnu::cold]] bool replay_record_(const snapshot_order_record& rec) noexcept {
        if (rec.px >= Ticks)
            return false;
        auto* o = book_.arena().allocate();
        if (o == nullptr)
            return false;
        o->id = rec.id;
        o->remaining = rec.remaining;
        o->px = rec.px;
        o->s = static_cast<side>(rec.s);
        o->t = static_cast<tif>(rec.t);
        o->_pad0 = 0;
        o->level_idx = rec.px;
        o->account_id = rec.account_id;
        if (o->s == side::bid)
            book_.bids().add(*o);
        else
            book_.asks().add(*o);
        book_.index().insert(rec.id, o);
        return true;
    }

    // Hot state on its own cache line. seq_ + last_* + have_top are touched
    // by every fill / cancel / modify; pub_ and cfg_ are read-mostly. Packing
    // the mutable cluster into a dedicated 64-byte line keeps prefetchers
    // happy and prevents publisher / engine writes from competing for the
    // same cache line under a producer / consumer split.
    struct alignas(64) hot_state {
        seq_t seq{0};
        tick_t last_bid_px{0};
        tick_t last_ask_px{0};
        qty_t last_bid_qty{0};
        qty_t last_ask_qty{0};
        bool have_top{false};
        bool top_dirty{false};
        // Whether each side held any order at the last published top. With the
        // cached best price they let a mutation decide, without a bitmap walk,
        // if it could move the top. See affects_top_.
        bool have_bid_side{false};
        bool have_ask_side{false};
        // Suppresses nested top_msg emission from publish_top_if_changed_.
        // Composite operations that perform multiple book mutations (e.g.
        // a price-change modify dispatched as cancel + submit) bump this
        // depth around the sub-calls so callers see a single coalesced
        // top_msg rather than one per sub-step.
        std::uint8_t suppress_top_depth{0};
    };

    P& pub_;
    engine_config cfg_;
    book<Ticks, MaxOrders> book_{};
    hot_state state_{};
};

}  // namespace lob

#endif  // LOB_ENGINE_HPP
