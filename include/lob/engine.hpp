#ifndef LOB_ENGINE_HPP
#define LOB_ENGINE_HPP

#include <lob/book.hpp>
#include <lob/concepts.hpp>
#include <lob/config.hpp>
#include <lob/messages.hpp>
#include <lob/order.hpp>
#include <lob/types.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>

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
public:
    engine(P& pub, engine_config cfg) noexcept : pub_(pub), cfg_(cfg) {}

    engine(engine const&)            = delete;
    engine(engine&&)                 = delete;
    engine& operator=(engine const&) = delete;
    engine& operator=(engine&&)      = delete;
    ~engine()                        = default;

    void on_submit(submit_msg const& m) noexcept {
        if (m.s == side::bid) handle_submit_<side::bid>(m);
        else                  handle_submit_<side::ask>(m);
        publish_top_if_changed_();
    }

    void on_cancel(cancel_msg const& m) noexcept {
        auto* o = book_.index().lookup(m.id);
        if (o == nullptr) return;
        if (o->s == side::bid) book_.bids().remove(*o);
        else                   book_.asks().remove(*o);
        book_.index().erase(o->id);
        book_.arena().deallocate(o);
        publish_top_if_changed_();
    }

    void on_modify(modify_msg const& m) noexcept {
        auto* o = book_.index().lookup(m.id);
        if (o == nullptr) return;
        const auto s = o->s;
        const auto t = o->t;
        if (m.new_px == o->px) {
            // qty-only fast path: mutate aggregate in place
            if (s == side::bid) {
                auto& lvl = book_.bids().level_at(o->px);
                lvl.aggregate = lvl.aggregate - o->remaining + m.new_qty;
            } else {
                auto& lvl = book_.asks().level_at(o->px);
                lvl.aggregate = lvl.aggregate - o->remaining + m.new_qty;
            }
            o->remaining = m.new_qty;
            publish_top_if_changed_();
            return;
        }
        // price change: cancel + resubmit at new price (loses time priority)
        on_cancel(cancel_msg{.id = m.id});
        on_submit(submit_msg{.id = m.id, .px = m.new_px, .qty = m.new_qty, .s = s, .t = t});
    }

    [[nodiscard]] book<Ticks, MaxOrders> const& book_view() const noexcept { return book_; }
    [[nodiscard]] seq_t                          last_seq()  const noexcept { return seq_; }

private:
    template <side Side>
    void handle_submit_(submit_msg const& m) noexcept {
        constexpr auto Opp = (Side == side::bid) ? side::ask : side::bid;

        // FOK precheck: walk opposite levels from best toward `m.px` and sum
        // their aggregates; abort all if the total is less than the request.
        if (m.t == tif::fok) {
            if (!can_fully_fill_<Opp>(m.px, m.qty)) return;
        }

        qty_t remaining = m.qty;
        match_against_opposite_<Side>(m, remaining);

        if (remaining == 0) return;

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
    void match_against_opposite_(submit_msg const& m, qty_t& remaining) noexcept {
        constexpr auto Opp = (Side == side::bid) ? side::ask : side::bid;
        auto& opp = side_<Opp>();

        while (remaining > 0) {
            const auto best = opp.best();
            if (!best.has_value()) return;
            const auto best_px = *best;
            if constexpr (Side == side::bid) {
                if (m.px < best_px) return;
            } else {
                if (m.px > best_px) return;
            }

            auto& lvl = opp.level_at(best_px);
            while (remaining > 0 && !lvl.empty()) {
                auto&      maker     = lvl.fifo.front();
                const auto trade_qty = std::min(remaining, maker.remaining);

                ++seq_;
                pub_.publish(fill_msg{
                    .maker = maker.id,
                    .taker = m.id,
                    .px    = best_px,
                    .qty   = trade_qty,
                    .seq   = seq_,
                });
                pub_.publish(trade_msg{.px = best_px, .qty = trade_qty, .seq = seq_});

                maker.remaining -= trade_qty;
                lvl.aggregate   -= trade_qty;
                remaining       -= trade_qty;

                if (maker.remaining == 0) {
                    auto*      victim    = &maker;
                    const auto victim_id = victim->id;
                    lvl.fifo.pop_front();
                    book_.index().erase(victim_id);
                    book_.arena().deallocate(victim);
                }
            }

            if (lvl.empty()) opp.notify_level_emptied(best_px);
        }
    }

    template <side Side>
    void rest_(submit_msg const& m, qty_t remaining) noexcept {
        auto* o = book_.arena().allocate();
        if (o == nullptr) return;  // arena exhausted; gateway-side concern
        o->id        = m.id;
        o->remaining = remaining;
        o->px        = m.px;
        o->s         = Side;
        o->t         = m.t;
        o->level_idx = m.px;
        side_<Side>().add(*o);
        book_.index().insert(m.id, o);
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
                if (total >= want) return true;
                px = book_.asks().next_populated_at_or_after(*px + 1);
            }
        } else {
            // aggressor is ask; walk bids descending from best to aggressor_px
            auto px = book_.bids().best();
            while (px.has_value() && *px >= aggressor_px) {
                total += book_.bids().aggregate_at(*px);
                if (total >= want) return true;
                if (*px == 0) break;
                px = book_.bids().prev_populated_at_or_before(*px - 1);
            }
        }
        return total >= want;
    }

    void publish_top_if_changed_() noexcept {
        const auto bb = book_.bids().best();
        const auto ba = book_.asks().best();

        const tick_t bid_px  = bb.has_value() ? *bb : tick_t{0};
        const tick_t ask_px  = ba.has_value() ? *ba : tick_t{0};
        const qty_t  bid_qty = bb.has_value() ? book_.bids().aggregate_at(*bb) : qty_t{0};
        const qty_t  ask_qty = ba.has_value() ? book_.asks().aggregate_at(*ba) : qty_t{0};

        if (cfg_.top_throttle && have_top_ &&
            bid_px == last_bid_px_ && ask_px == last_ask_px_ &&
            bid_qty == last_bid_qty_ && ask_qty == last_ask_qty_) {
            return;
        }

        ++seq_;
        pub_.publish(top_msg{
            .bid_px = bid_px,
            .ask_px = ask_px,
            .bid_qty = bid_qty,
            .ask_qty = ask_qty,
            .seq = seq_,
        });
        last_bid_px_  = bid_px;
        last_ask_px_  = ask_px;
        last_bid_qty_ = bid_qty;
        last_ask_qty_ = ask_qty;
        have_top_     = true;
    }

    template <side S>
    [[nodiscard]] book_side<Ticks, S>& side_() noexcept {
        if constexpr (S == side::bid) return book_.bids();
        else                          return book_.asks();
    }

    P&                       pub_;
    engine_config            cfg_;
    book<Ticks, MaxOrders>   book_{};
    seq_t                    seq_{0};
    tick_t                   last_bid_px_{0};
    tick_t                   last_ask_px_{0};
    qty_t                    last_bid_qty_{0};
    qty_t                    last_ask_qty_{0};
    bool                     have_top_{false};
};

}  // namespace lob

#endif  // LOB_ENGINE_HPP
