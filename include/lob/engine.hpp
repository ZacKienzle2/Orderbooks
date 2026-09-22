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

// engine<P, Ticks, MaxOrders, Lookup>
// -----------------------------------
// Single-symbol matching engine. The engine owns a book and a sequence
// counter; it does not own the publisher (caller manages its lifetime).
//
// Hot-path methods (on_submit, on_cancel, on_modify) are noexcept by
// contract. The gateway is responsible for upstream validation of px, qty,
// side, tif, and ids; the engine treats out-of-range inputs as UB. Order
// ids are nonzero, since zero is modify_msg::new_id's keep-the-id sentinel.
//
// Matching follows strict price-time priority. When an aggressor crosses
// the opposite best, the engine walks that level's FIFO from the front,
// consuming maker orders until the aggressor is filled or the price stops
// crossing. Each fill emits a fill_msg with a monotonic seq. Top-of-book
// changes emit a top_msg subject to engine_config::top_throttle.
//
// Lookup picks how a cancel or modify reaches its order. With by_id the engine
// keeps an id index and ignores the handle fields. With by_handle it keeps
// none. Each order that rests returns a handle instead, and a cancel or modify
// must carry it, which reaches the order with no lookup at all (ADR-0042). A
// gateway that hands clients an exchange order id, FIX OrderID(37), can map it
// to the handle. Restore reissues handles, so such a gateway rebuilds its map
// from the restored book. The choice is a template parameter, so an engine of
// either kind carries no code for the other.
template <publisher P,
          std::size_t Ticks,
          std::size_t MaxOrders,
          order_lookup Lookup = order_lookup::by_id>
class engine {
    static constexpr bool by_handle_ = Lookup == order_lookup::by_handle;

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
    static_assert(noexcept(std::declval<P&>().publish(std::declval<const reject_msg&>())),
                  "publisher::publish(reject_msg) must be noexcept");

   public:
    engine(P& pub, engine_config cfg) noexcept : pub_(pub), cfg_(cfg) {
        state_.seq = cfg_.seq_base;
    }

    engine(const engine&) = delete;
    engine(engine&&) = delete;
    engine& operator=(const engine&) = delete;
    engine& operator=(engine&&) = delete;
    ~engine() = default;

    // Returns the handle of the order left resting, or an empty one when none
    // rests or the engine looks orders up by id.
    [[gnu::hot]] order_handle on_submit(const submit_msg& m) noexcept {
        const order_handle h =
            (m.s == side::bid) ? handle_submit_<side::bid>(m) : handle_submit_<side::ask>(m);
        publish_top_if_changed_();
        return h;
    }

    [[gnu::hot]] void on_cancel(const cancel_msg& m) noexcept {
        auto* o = find_(m.id, m.handle, /*unindex=*/true);
        if (o == nullptr)
            return;
        const auto cancel_side = o->s;
        const auto cancel_px = o->px;
        if (o->s == side::bid)
            book_.bids().remove(*o);
        else
            book_.asks().remove(*o);
        book_.arena().deallocate(o);
        mark_top_(cancel_side, cancel_px);
        publish_top_if_changed_();
    }

    // Returns the order's handle after the modify. That is the handle the
    // modify carried, unless a crossing price change cancels and resubmits the
    // order, which rests under a new handle or leaves the book with none. The
    // common paths therefore read nothing to build one.
    [[gnu::hot]] order_handle on_modify(const modify_msg& m) noexcept {
        // ClOrdID chain. A cancel-replace names the order by its current id
        // and may assign the next one; rename the id_index entry first so
        // every path below, including the crossing cancel + resubmit, works
        // with the order's new identity. A rename takes the old entry out in
        // the probe that finds it. Priority is governed solely by the px / qty
        // branches below.
        const bool rename = m.new_id != 0 && m.new_id != m.id;
        auto* o = find_(m.id, m.handle, /*unindex=*/rename);
        if (o == nullptr)
            return {};
        if (rename) {
            o->id = m.new_id;
            if constexpr (!by_handle_)
                book_.index().insert(m.new_id, o);
        }
        const auto effective_id = o->id;
        const auto s = o->s;
        const auto t = o->t;
        if (m.new_px == o->px) {
            if (m.new_qty == o->remaining)
                return m.handle;  // no-op beyond any id chain applied above
            // Qty-only fast path. Mutate the level aggregate in place.
            //
            // Spec choice, deliberate: a quantity change at the same price
            // KEEPS time priority in both directions. Nasdaq and CME demote
            // an increase to the back of the queue; this engine does not,
            // because demotion turns the O(1) in-place mutation into an
            // unlink and relink and the venues that demote do so as market
            // policy, not matching necessity. A gateway targeting such a
            // venue's semantics should send cancel + resubmit for increases.
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
            return m.handle;
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
                return m.handle;
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
                return m.handle;
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
        on_cancel(cancel_msg{.id = effective_id, .handle = m.handle});
        const order_handle resubmitted = on_submit(submit_msg{.id = effective_id,
                                                              .px = m.new_px,
                                                              .qty = m.new_qty,
                                                              .s = s,
                                                              .t = t,
                                                              ._pad = 0,
                                                              .account_id = acct});
        --state_.suppress_top_depth;
        publish_top_if_changed_();
        return resubmitted;
    }

    // Prefetch hint for a command that will be applied shortly. The engine is
    // bound by a chain of dependent cache misses per command, and a single
    // command offers nothing to overlap them with. A consumer holding a batch
    // calls this for the command a few positions ahead, so its first misses
    // are in flight while the earlier commands run. This is the group
    // prefetching of Chen, Ailamaki, Gibbons and Mowry for hash joins
    // (doi:10.1145/1272743.1272747), with the batch the ingress ring already
    // delivers as the group. The first stage starts the level a submit rests
    // at, and the order and slot generation a handle names, two independent
    // misses in place of a chain. It reads the message, writes nothing, and
    // never changes what a command does.
    void prefetch(const command& c) const noexcept {
        switch (c.k) {
            case command::kind::submit: {
                const auto& m = c.body.submit;
                if (m.s == side::bid)
                    book_.bids().prefetch_level(m.px);
                else
                    book_.asks().prefetch_level(m.px);
                break;
            }
            case command::kind::cancel:
                if constexpr (by_handle_)
                    prefetch_slot_(c.body.cancel.handle);
                break;
            case command::kind::modify:
                if constexpr (by_handle_)
                    prefetch_slot_(c.body.modify.handle);
                break;
        }
    }

    // Second stage, called closer to the command than prefetch. It starts the
    // order a cancel or modify names by id, found through the index, and the
    // tail order a resting submit links behind, whose level the first stage
    // brought in.
    void prefetch_order(const command& c) const noexcept {
        switch (c.k) {
            case command::kind::submit: {
                const auto& m = c.body.submit;
                if (m.s == side::bid)
                    prefetch_tail_(book_.bids().level_at(m.px));
                else
                    prefetch_tail_(book_.asks().level_at(m.px));
                break;
            }
            case command::kind::cancel:
                if constexpr (!by_handle_)
                    if (const order* o = book_.index().lookup(c.body.cancel.id))
                        __builtin_prefetch(o, 1, 3);
                break;
            case command::kind::modify:
                if constexpr (!by_handle_)
                    if (const order* o = book_.index().lookup(c.body.modify.id))
                        __builtin_prefetch(o, 1, 3);
                break;
        }
    }

    // Serialise the engine's complete state into a snapshot_sink.
    //
    // The wire layout is a snapshot_header followed by num_orders
    // snapshot_order_records. Records are emitted in (price ascending, FIFO
    // front-to-back) order per side, bids before asks. restore() replays that
    // exact order to reproduce FIFO time priority at each level.
    //
    // snapshot() propagates any exception thrown by the sink's write().
    // Production sinks (fixed buffers, mmap-backed files, ring publishers)
    // should mark write() noexcept. Growable sinks like vector_snapshot_buffer
    // can throw std::bad_alloc on memory pressure, leaving engine state
    // unaffected because snapshot() never mutates.
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
        // The header byte comes from an external source, and casting a value
        // outside the enumerators is UB before any switch could reject it.
        if (hdr.self_cross > static_cast<std::uint8_t>(self_cross_policy::decrement_trade))
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

    [[nodiscard]] const book<Ticks, MaxOrders, by_handle_>& book_view() const noexcept {
        return book_;
    }

    [[nodiscard]] const engine_config& config() const noexcept { return cfg_; }

    [[nodiscard]] seq_t last_seq() const noexcept { return state_.seq; }

   private:
    template <side Side>
    order_handle handle_submit_(const submit_msg& m) noexcept {
        constexpr auto Opp = (Side == side::bid) ? side::ask : side::bid;

        // FOK precheck. Walk opposite levels from best toward `m.px` and sum
        // the quantity the match loop could actually consume, aborting the
        // whole order if the total is short.
        if (m.t == tif::fok) {
            if (!can_fully_fill_<Opp>(m.px, m.qty, m.account_id))
                return {};
        }

        qty_t remaining = m.qty;
        match_against_opposite_<Side>(m, remaining);

        if (remaining == 0)
            return {};

        switch (m.t) {
            case tif::ioc:
            case tif::fok:
                // IOC always drops the residual. FOK never reaches here with
                // residual > 0 unless we crossed multiple levels and lost
                // qty to rounding (impossible with integer qty); drop too.
                return {};
            case tif::gtc:
            default:
                return rest_<Side>(m, remaining);
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
                    if constexpr (!by_handle_)
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
    bool handle_self_cross_(const submit_msg& m,
                            order& maker,
                            level& lvl,
                            tick_t best_px,
                            qty_t& remaining) noexcept {
        switch (cfg_.self_cross) {
            case self_cross_policy::cancel_newest:
                remaining = 0;
                return true;
            case self_cross_policy::cancel_oldest: {
                auto* victim = &maker;
                const auto victim_id = victim->id;
                lvl.aggregate -= maker.remaining;
                lvl.fifo.pop_front();
                if constexpr (!by_handle_)
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
                    if constexpr (!by_handle_)
                        book_.index().erase(victim_id);
                    book_.arena().deallocate(victim);
                }
                return false;
            }
        }
        return false;
    }

    template <side Side>
    order_handle rest_(const submit_msg& m, qty_t remaining) noexcept {
        auto* o = book_.arena().allocate();
        if (o == nullptr) [[unlikely]] {
            // Arena exhausted; the residual cannot rest. Publish the loss
            // instead of dropping it silently, so a gateway can reject its
            // ack and downstream risk sees the shortfall. The book is
            // untouched, so no top update follows.
            ++state_.seq;
            pub_.publish(reject_msg{.id = m.id,
                                    .account = m.account_id,
                                    .px = m.px,
                                    .qty = remaining,
                                    .reason = reject_reason::arena_full,
                                    .seq = state_.seq});
            return {};
        }
        o->id = m.id;
        o->remaining = remaining;
        o->px = m.px;
        o->s = Side;
        o->t = m.t;
        o->_pad0 = 0;
        o->account_id = m.account_id;
        side_<Side>().add(*o);
        mark_top_(Side, m.px);
        if constexpr (by_handle_) {
            return handle_of_(o);
        } else {
            book_.index().insert(m.id, o);
            return {};
        }
    }

    template <side Opp>
    [[nodiscard]] bool can_fully_fill_(tick_t aggressor_px,
                                       qty_t want,
                                       account_id_t acct) const noexcept {
        // Walk opposite-side levels from best toward aggressor_px, summing
        // the quantity the match loop could consume. For bid aggressor we
        // walk ask levels from lowest toward aggressor_px (inclusive). For
        // ask aggressor we walk bid levels from highest toward aggressor_px
        // (inclusive).
        //
        // With no account, or under decrement_trade, every unit resting at a
        // crossing level consumes the aggressor (by fill or by self-trade
        // netting), so the O(1) level aggregate is exact. With an account
        // under the cancelling policies the aggregate overstates what is
        // fillable, because it includes the aggressor's own resting orders,
        // which the match loop destroys (cancel_oldest) or aborts on
        // (cancel_newest) rather than fills. Walk the level FIFOs instead:
        // skip own-account makers under cancel_oldest, and stop the whole
        // walk at the first own-account maker under cancel_newest, exactly
        // where the match loop itself would stop.
        const bool all_consumable =
            acct == 0 || cfg_.self_cross == self_cross_policy::decrement_trade;
        qty_t total = 0;
        bool aborted = false;
        const auto consume_level = [&](const auto& bs, tick_t px) noexcept {
            if (all_consumable) {
                total += bs.aggregate_at(px);
                return total >= want;
            }
            for (const auto& o : bs.level_at(px).fifo) {
                if (o.account_id == acct) {
                    if (cfg_.self_cross == self_cross_policy::cancel_newest) {
                        aborted = true;
                        return true;
                    }
                    continue;
                }
                total += o.remaining;
                if (total >= want)
                    return true;
            }
            return false;
        };
        if constexpr (Opp == side::ask) {
            // aggressor is bid; walk asks ascending from best to aggressor_px
            auto px = book_.asks().best();
            while (px.has_value() && *px <= aggressor_px) {
                if (consume_level(book_.asks(), *px))
                    break;
                px = book_.asks().next_populated_at_or_after(*px + 1);
            }
        } else {
            // aggressor is ask; walk bids descending from best to aggressor_px
            auto px = book_.bids().best();
            while (px.has_value() && *px >= aggressor_px) {
                if (consume_level(book_.bids(), *px))
                    break;
                if (*px == 0)
                    break;
                px = book_.bids().prev_populated_at_or_before(*px - 1);
            }
        }
        return !aborted && total >= want;
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

    // The order a cancel or modify names, through its handle on an engine that
    // issues them and through the id index otherwise, which with unindex set
    // gives up the entry in the probe that finds it.
    [[nodiscard]] order* find_(order_id_t id, order_handle h, bool unindex) noexcept {
        if constexpr (by_handle_)
            return resolve_(h, id);
        else
            return unindex ? book_.index().take(id) : book_.index().lookup(id);
    }

    [[nodiscard]] order_handle handle_of_(const order* o) const noexcept
        requires by_handle_
    {
        const auto idx = book_.arena().index_of(o);
        return {.slot = static_cast<std::uint32_t>(idx),
                .generation = book_.arena().generation(idx)};
    }

    // The order a live handle names, confirmed by id. A handle whose slot has
    // been freed carries a generation the slot no longer has, one whose slot
    // was reused names an order with another id, and the empty handle has an
    // even generation, so none of them finds anything.
    [[nodiscard]] order* resolve_(order_handle h, order_id_t id) noexcept
        requires by_handle_
    {
        if (h.slot >= MaxOrders || (h.generation & 1U) == 0 ||
            book_.arena().generation(h.slot) != h.generation)
            return nullptr;
        order* o = book_.arena().slot_at(h.slot);
        return o->id == id ? o : nullptr;
    }

    void prefetch_slot_(order_handle h) const noexcept
        requires by_handle_
    {
        if (h.slot < MaxOrders) {
            __builtin_prefetch(book_.arena().slot_address(h.slot), 1, 3);
            book_.arena().prefetch_generation(h.slot);
        }
    }

    static void prefetch_tail_(const level& lvl) noexcept {
        if (!lvl.fifo.empty())
            __builtin_prefetch(&lvl.fifo.back(), 1, 3);
    }

    template <side S>
    [[nodiscard]] book_side<Ticks, S>& side_() noexcept {
        if constexpr (S == side::bid)
            return book_.bids();
        else
            return book_.asks();
    }

    [[nodiscard]] [[gnu::cold]] std::uint64_t count_resting_() const noexcept {
        // Cold, once per snapshot(). for_each_level walks the bitmap, so
        // empty stretches of a sparse ladder cost a tier step each.
        std::uint64_t count = 0;
        const auto add = [&count](tick_t, const level& lvl) noexcept {
            count += lvl.order_count();
        };
        book_.bids().for_each_level(add);
        book_.asks().for_each_level(add);
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
        // Snapshot contract. Records are emitted in (price ascending,
        // FIFO front-to-back) order so restore() replays them in the
        // same order and reproduces FIFO time priority at each level.
        // for_each_level walks ascending on either side, whichever end of
        // the ladder holds that side's best.
        const auto emit_level = [&sink](tick_t, const level& lvl) {
            for (const auto& o : lvl.fifo) {
                snapshot_order_record rec{};
                rec.id = o.id;
                rec.remaining = o.remaining;
                rec.px = o.px;
                rec.s = static_cast<std::uint8_t>(Side);
                rec.t = static_cast<std::uint8_t>(o.t);
                rec.account_id = o.account_id;
                emit_bytes_(sink, &rec, sizeof(rec));
            }
        };
        if constexpr (Side == side::bid)
            book_.bids().for_each_level(emit_level);
        else
            book_.asks().for_each_level(emit_level);
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
                if constexpr (!by_handle_)
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
                if constexpr (!by_handle_)
                    book_.index().erase(victim->id);
                book_.arena().deallocate(victim);
            }
        }
        state_ = hot_state{};
        state_.seq = cfg_.seq_base;
    }

    [[gnu::cold]] bool replay_record_(const snapshot_order_record& rec) noexcept {
        // Records come from an external source. Reject anything the engine
        // could never have emitted before the bytes reach an enum cast, an
        // unchecked ladder index, the id_index, or a resting zero-quantity
        // order. px must be on the ladder, s and t must be enumerators, a
        // resting order always has quantity, and ids are nonzero because zero
        // is modify's keep-the-id sentinel.
        if (rec.id == 0)
            return false;
        if (rec.px >= Ticks)
            return false;
        if (rec.s > static_cast<std::uint8_t>(side::ask))
            return false;
        if (rec.t > static_cast<std::uint8_t>(tif::fok))
            return false;
        if (rec.remaining == 0 || rec.remaining > cfg_.max_order_qty)
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
        o->account_id = rec.account_id;
        if (o->s == side::bid)
            book_.bids().add(*o);
        else
            book_.asks().add(*o);
        if constexpr (!by_handle_)
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
    book<Ticks, MaxOrders, by_handle_> book_{};
    hot_state state_{};
};

}  // namespace lob

#endif  // LOB_ENGINE_HPP
